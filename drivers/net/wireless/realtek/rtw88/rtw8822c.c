// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8822c.h"
#include "rtw8822c_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"
#include "util.h"
#include "bf.h"
#include "efuse.h"

#define IQK_DONE_8822C 0xaa

static void rtw8822c_config_trx_mode(struct rtw_dev *rtwdev, u8 tx_path,
				     u8 rx_path, bool is_tx2_path);

static void rtw8822ce_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8822c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static void rtw8822cu_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8822c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->u.mac_addr);
}

static void rtw8822cs_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8822c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->s.mac_addr);
}

static int rtw8822c_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8822c_efuse *map;
	int i;

	map = (struct rtw8822c_efuse *)log_map;

	efuse->usb_mode_switch = u8_get_bits(map->usb_mode, BIT(7));
	efuse->rfe_option = map->rfe_option;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k & XCAP_MASK;
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[RF_PATH_A] = map->path_a_thermal;
	efuse->thermal_meter[RF_PATH_B] = map->path_b_thermal;
	efuse->thermal_meter_k =
			(map->path_a_thermal + map->path_b_thermal) >> 1;
	efuse->power_track_type = (map->tx_pwr_calibrate_rate >> 4) & 0xf;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8822ce_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_USB:
		rtw8822cu_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_SDIO:
		rtw8822cs_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -ENOTSUPP;
	}

	return 0;
}

static void rtw8822c_header_file_init(struct rtw_dev *rtwdev, bool pre)
{
	rtw_write32_set(rtwdev, REG_3WIRE, BIT_3WIRE_TX_EN | BIT_3WIRE_RX_EN);
	rtw_write32_set(rtwdev, REG_3WIRE, BIT_3WIRE_PI_ON);
	rtw_write32_set(rtwdev, REG_3WIRE2, BIT_3WIRE_TX_EN | BIT_3WIRE_RX_EN);
	rtw_write32_set(rtwdev, REG_3WIRE2, BIT_3WIRE_PI_ON);

	if (pre)
		rtw_write32_clr(rtwdev, REG_ENCCK, BIT_CCK_OFDM_BLK_EN);
	else
		rtw_write32_set(rtwdev, REG_ENCCK, BIT_CCK_OFDM_BLK_EN);
}

static void rtw8822c_bb_reset(struct rtw_dev *rtwdev)
{
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
	rtw_write16_clr(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_BB_RSTB);
}

static void rtw8822c_dac_backup_reg(struct rtw_dev *rtwdev,
				    struct rtw_backup_info *backup,
				    struct rtw_backup_info *backup_rf)
{
	u32 path, i;
	u32 val;
	u32 reg;
	u32 rf_addr[DACK_RF_8822C] = {0x8f};
	u32 addrs[DACK_REG_8822C] = {0x180c, 0x1810, 0x410c, 0x4110,
				     0x1c3c, 0x1c24, 0x1d70, 0x9b4,
				     0x1a00, 0x1a14, 0x1d58, 0x1c38,
				     0x1e24, 0x1e28, 0x1860, 0x4160};

	for (i = 0; i < DACK_REG_8822C; i++) {
		backup[i].len = 4;
		backup[i].reg = addrs[i];
		backup[i].val = rtw_read32(rtwdev, addrs[i]);
	}

	for (path = 0; path < DACK_PATH_8822C; path++) {
		for (i = 0; i < DACK_RF_8822C; i++) {
			reg = rf_addr[i];
			val = rtw_read_rf(rtwdev, path, reg, RFREG_MASK);
			backup_rf[path * i + i].reg = reg;
			backup_rf[path * i + i].val = val;
		}
	}
}

static void rtw8822c_dac_restore_reg(struct rtw_dev *rtwdev,
				     struct rtw_backup_info *backup,
				     struct rtw_backup_info *backup_rf)
{
	u32 path, i;
	u32 val;
	u32 reg;

	rtw_restore_reg(rtwdev, backup, DACK_REG_8822C);

	for (path = 0; path < DACK_PATH_8822C; path++) {
		for (i = 0; i < DACK_RF_8822C; i++) {
			val = backup_rf[path * i + i].val;
			reg = backup_rf[path * i + i].reg;
			rtw_write_rf(rtwdev, path, reg, RFREG_MASK, val);
		}
	}
}

static void rtw8822c_rf_minmax_cmp(struct rtw_dev *rtwdev, u32 value,
				   u32 *min, u32 *max)
{
	if (value >= 0x200) {
		if (*min >= 0x200) {
			if (*min > value)
				*min = value;
		} else {
			*min = value;
		}
		if (*max >= 0x200) {
			if (*max < value)
				*max = value;
		}
	} else {
		if (*min < 0x200) {
			if (*min > value)
				*min = value;
		}

		if (*max  >= 0x200) {
			*max = value;
		} else {
			if (*max < value)
				*max = value;
		}
	}
}

static void __rtw8822c_dac_iq_sort(struct rtw_dev *rtwdev, u32 *v1, u32 *v2)
{
	if (*v1 >= 0x200 && *v2 >= 0x200) {
		if (*v1 > *v2)
			swap(*v1, *v2);
	} else if (*v1 < 0x200 && *v2 < 0x200) {
		if (*v1 > *v2)
			swap(*v1, *v2);
	} else if (*v1 < 0x200 && *v2 >= 0x200) {
		swap(*v1, *v2);
	}
}

static void rtw8822c_dac_iq_sort(struct rtw_dev *rtwdev, u32 *iv, u32 *qv)
{
	u32 i, j;

	for (i = 0; i < DACK_SN_8822C - 1; i++) {
		for (j = 0; j < (DACK_SN_8822C - 1 - i) ; j++) {
			__rtw8822c_dac_iq_sort(rtwdev, &iv[j], &iv[j + 1]);
			__rtw8822c_dac_iq_sort(rtwdev, &qv[j], &qv[j + 1]);
		}
	}
}

static void rtw8822c_dac_iq_offset(struct rtw_dev *rtwdev, u32 *vec, u32 *val)
{
	u32 p, m, t, i;

	m = 0;
	p = 0;
	for (i = 10; i < DACK_SN_8822C - 10; i++) {
		if (vec[i] > 0x200)
			m = (0x400 - vec[i]) + m;
		else
			p = vec[i] + p;
	}

	if (p > m) {
		t = p - m;
		t = t / (DACK_SN_8822C - 20);
	} else {
		t = m - p;
		t = t / (DACK_SN_8822C - 20);
		if (t != 0x0)
			t = 0x400 - t;
	}

	*val = t;
}

static u32 rtw8822c_get_path_write_addr(u8 path)
{
	u32 base_addr;

	switch (path) {
	case RF_PATH_A:
		base_addr = 0x1800;
		break;
	case RF_PATH_B:
		base_addr = 0x4100;
		break;
	default:
		WARN_ON(1);
		return -1;
	}

	return base_addr;
}

static u32 rtw8822c_get_path_read_addr(u8 path)
{
	u32 base_addr;

	switch (path) {
	case RF_PATH_A:
		base_addr = 0x2800;
		break;
	case RF_PATH_B:
		base_addr = 0x4500;
		break;
	default:
		WARN_ON(1);
		return -1;
	}

	return base_addr;
}

static bool rtw8822c_dac_iq_check(struct rtw_dev *rtwdev, u32 value)
{
	bool ret = true;

	if ((value >= 0x200 && (0x400 - value) > 0x64) ||
	    (value < 0x200 && value > 0x64)) {
		ret = false;
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] Error overflow\n");
	}

	return ret;
}

static void rtw8822c_dac_cal_iq_sample(struct rtw_dev *rtwdev, u32 *iv, u32 *qv)
{
	u32 temp;
	int i = 0, cnt = 0;

	while (i < DACK_SN_8822C && cnt < 10000) {
		cnt++;
		temp = rtw_read32_mask(rtwdev, 0x2dbc, 0x3fffff);
		iv[i] = (temp & 0x3ff000) >> 12;
		qv[i] = temp & 0x3ff;

		if (rtw8822c_dac_iq_check(rtwdev, iv[i]) &&
		    rtw8822c_dac_iq_check(rtwdev, qv[i]))
			i++;
	}
}

static void rtw8822c_dac_cal_iq_search(struct rtw_dev *rtwdev,
				       u32 *iv, u32 *qv,
				       u32 *i_value, u32 *q_value)
{
	u32 i_max = 0, q_max = 0, i_min = 0, q_min = 0;
	u32 i_delta, q_delta;
	u32 temp;
	int i, cnt = 0;

	do {
		i_min = iv[0];
		i_max = iv[0];
		q_min = qv[0];
		q_max = qv[0];
		for (i = 0; i < DACK_SN_8822C; i++) {
			rtw8822c_rf_minmax_cmp(rtwdev, iv[i], &i_min, &i_max);
			rtw8822c_rf_minmax_cmp(rtwdev, qv[i], &q_min, &q_max);
		}

		if (i_max < 0x200 && i_min < 0x200)
			i_delta = i_max - i_min;
		else if (i_max >= 0x200 && i_min >= 0x200)
			i_delta = i_max - i_min;
		else
			i_delta = i_max + (0x400 - i_min);

		if (q_max < 0x200 && q_min < 0x200)
			q_delta = q_max - q_min;
		else if (q_max >= 0x200 && q_min >= 0x200)
			q_delta = q_max - q_min;
		else
			q_delta = q_max + (0x400 - q_min);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[DACK] i: min=0x%08x, max=0x%08x, delta=0x%08x\n",
			i_min, i_max, i_delta);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[DACK] q: min=0x%08x, max=0x%08x, delta=0x%08x\n",
			q_min, q_max, q_delta);

		rtw8822c_dac_iq_sort(rtwdev, iv, qv);

		if (i_delta > 5 || q_delta > 5) {
			temp = rtw_read32_mask(rtwdev, 0x2dbc, 0x3fffff);
			iv[0] = (temp & 0x3ff000) >> 12;
			qv[0] = temp & 0x3ff;
			temp = rtw_read32_mask(rtwdev, 0x2dbc, 0x3fffff);
			iv[DACK_SN_8822C - 1] = (temp & 0x3ff000) >> 12;
			qv[DACK_SN_8822C - 1] = temp & 0x3ff;
		} else {
			break;
		}
	} while (cnt++ < 100);

	rtw8822c_dac_iq_offset(rtwdev, iv, i_value);
	rtw8822c_dac_iq_offset(rtwdev, qv, q_value);
}

static void rtw8822c_dac_cal_rf_mode(struct rtw_dev *rtwdev,
				     u32 *i_value, u32 *q_value)
{
	u32 iv[DACK_SN_8822C], qv[DACK_SN_8822C];
	u32 rf_a, rf_b;

	rf_a = rtw_read_rf(rtwdev, RF_PATH_A, 0x0, RFREG_MASK);
	rf_b = rtw_read_rf(rtwdev, RF_PATH_B, 0x0, RFREG_MASK);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] RF path-A=0x%05x\n", rf_a);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] RF path-B=0x%05x\n", rf_b);

	rtw8822c_dac_cal_iq_sample(rtwdev, iv, qv);
	rtw8822c_dac_cal_iq_search(rtwdev, iv, qv, i_value, q_value);
}

static void rtw8822c_dac_bb_setting(struct rtw_dev *rtwdev)
{
	rtw_write32_mask(rtwdev, 0x1d58, 0xff8, 0x1ff);
	rtw_write32_mask(rtwdev, 0x1a00, 0x3, 0x2);
	rtw_write32_mask(rtwdev, 0x1a14, 0x300, 0x3);
	rtw_write32(rtwdev, 0x1d70, 0x7e7e7e7e);
	rtw_write32_mask(rtwdev, 0x180c, 0x3, 0x0);
	rtw_write32_mask(rtwdev, 0x410c, 0x3, 0x0);
	rtw_write32(rtwdev, 0x1b00, 0x00000008);
	rtw_write8(rtwdev, 0x1bcc, 0x3f);
	rtw_write32(rtwdev, 0x1b00, 0x0000000a);
	rtw_write8(rtwdev, 0x1bcc, 0x3f);
	rtw_write32_mask(rtwdev, 0x1e24, BIT(31), 0x0);
	rtw_write32_mask(rtwdev, 0x1e28, 0xf, 0x3);
}

static void rtw8822c_dac_cal_adc(struct rtw_dev *rtwdev,
				 u8 path, u32 *adc_ic, u32 *adc_qc)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 ic = 0, qc = 0, temp = 0;
	u32 base_addr;
	u32 path_sel;
	int i;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] ADCK path(%d)\n", path);

	base_addr = rtw8822c_get_path_write_addr(path);
	switch (path) {
	case RF_PATH_A:
		path_sel = 0xa0000;
		break;
	case RF_PATH_B:
		path_sel = 0x80000;
		break;
	default:
		WARN_ON(1);
		return;
	}

	/* ADCK step1 */
	rtw_write32_mask(rtwdev, base_addr + 0x30, BIT(30), 0x0);
	if (path == RF_PATH_B)
		rtw_write32(rtwdev, base_addr + 0x30, 0x30db8041);
	rtw_write32(rtwdev, base_addr + 0x60, 0xf0040ff0);
	rtw_write32(rtwdev, base_addr + 0x0c, 0xdff00220);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02dd08c4);
	rtw_write32(rtwdev, base_addr + 0x0c, 0x10000260);
	rtw_write_rf(rtwdev, RF_PATH_A, 0x0, RFREG_MASK, 0x10000);
	rtw_write_rf(rtwdev, RF_PATH_B, 0x0, RFREG_MASK, 0x10000);
	for (i = 0; i < 10; i++) {
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] ADCK count=%d\n", i);
		rtw_write32(rtwdev, 0x1c3c, path_sel + 0x8003);
		rtw_write32(rtwdev, 0x1c24, 0x00010002);
		rtw8822c_dac_cal_rf_mode(rtwdev, &ic, &qc);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[DACK] before: i=0x%x, q=0x%x\n", ic, qc);

		/* compensation value */
		if (ic != 0x0) {
			ic = 0x400 - ic;
			*adc_ic = ic;
		}
		if (qc != 0x0) {
			qc = 0x400 - qc;
			*adc_qc = qc;
		}
		temp = (ic & 0x3ff) | ((qc & 0x3ff) << 10);
		rtw_write32(rtwdev, base_addr + 0x68, temp);
		dm_info->dack_adck[path] = temp;
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] ADCK 0x%08x=0x08%x\n",
			base_addr + 0x68, temp);
		/* check ADC DC offset */
		rtw_write32(rtwdev, 0x1c3c, path_sel + 0x8103);
		rtw8822c_dac_cal_rf_mode(rtwdev, &ic, &qc);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[DACK] after:  i=0x%08x, q=0x%08x\n", ic, qc);
		if (ic >= 0x200)
			ic = 0x400 - ic;
		if (qc >= 0x200)
			qc = 0x400 - qc;
		if (ic < 5 && qc < 5)
			break;
	}

	/* ADCK step2 */
	rtw_write32(rtwdev, 0x1c3c, 0x00000003);
	rtw_write32(rtwdev, base_addr + 0x0c, 0x10000260);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c4);

	/* release pull low switch on IQ path */
	rtw_write_rf(rtwdev, path, 0x8f, BIT(13), 0x1);
}

static void rtw8822c_dac_cal_step1(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 base_addr;
	u32 read_addr;

	base_addr = rtw8822c_get_path_write_addr(path);
	read_addr = rtw8822c_get_path_read_addr(path);

	rtw_write32(rtwdev, base_addr + 0x68, dm_info->dack_adck[path]);
	rtw_write32(rtwdev, base_addr + 0x0c, 0xdff00220);
	if (path == RF_PATH_A) {
		rtw_write32(rtwdev, base_addr + 0x60, 0xf0040ff0);
		rtw_write32(rtwdev, 0x1c38, 0xffffffff);
	}
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c5);
	rtw_write32(rtwdev, 0x9b4, 0xdb66db00);
	rtw_write32(rtwdev, base_addr + 0xb0, 0x0a11fb88);
	rtw_write32(rtwdev, base_addr + 0xbc, 0x0008ff81);
	rtw_write32(rtwdev, base_addr + 0xc0, 0x0003d208);
	rtw_write32(rtwdev, base_addr + 0xcc, 0x0a11fb88);
	rtw_write32(rtwdev, base_addr + 0xd8, 0x0008ff81);
	rtw_write32(rtwdev, base_addr + 0xdc, 0x0003d208);
	rtw_write32(rtwdev, base_addr + 0xb8, 0x60000000);
	mdelay(2);
	rtw_write32(rtwdev, base_addr + 0xbc, 0x000aff8d);
	mdelay(2);
	rtw_write32(rtwdev, base_addr + 0xb0, 0x0a11fb89);
	rtw_write32(rtwdev, base_addr + 0xcc, 0x0a11fb89);
	mdelay(1);
	rtw_write32(rtwdev, base_addr + 0xb8, 0x62000000);
	rtw_write32(rtwdev, base_addr + 0xd4, 0x62000000);
	mdelay(20);
	if (!check_hw_ready(rtwdev, read_addr + 0x08, 0x7fff80, 0xffff) ||
	    !check_hw_ready(rtwdev, read_addr + 0x34, 0x7fff80, 0xffff))
		rtw_err(rtwdev, "failed to wait for dack ready\n");
	rtw_write32(rtwdev, base_addr + 0xb8, 0x02000000);
	mdelay(1);
	rtw_write32(rtwdev, base_addr + 0xbc, 0x0008ff87);
	rtw_write32(rtwdev, 0x9b4, 0xdb6db600);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c5);
	rtw_write32(rtwdev, base_addr + 0xbc, 0x0008ff87);
	rtw_write32(rtwdev, base_addr + 0x60, 0xf0000000);
}

static void rtw8822c_dac_cal_step2(struct rtw_dev *rtwdev,
				   u8 path, u32 *ic_out, u32 *qc_out)
{
	u32 base_addr;
	u32 ic, qc, ic_in, qc_in;

	base_addr = rtw8822c_get_path_write_addr(path);
	rtw_write32_mask(rtwdev, base_addr + 0xbc, 0xf0000000, 0x0);
	rtw_write32_mask(rtwdev, base_addr + 0xc0, 0xf, 0x8);
	rtw_write32_mask(rtwdev, base_addr + 0xd8, 0xf0000000, 0x0);
	rtw_write32_mask(rtwdev, base_addr + 0xdc, 0xf, 0x8);

	rtw_write32(rtwdev, 0x1b00, 0x00000008);
	rtw_write8(rtwdev, 0x1bcc, 0x03f);
	rtw_write32(rtwdev, base_addr + 0x0c, 0xdff00220);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c5);
	rtw_write32(rtwdev, 0x1c3c, 0x00088103);

	rtw8822c_dac_cal_rf_mode(rtwdev, &ic_in, &qc_in);
	ic = ic_in;
	qc = qc_in;

	/* compensation value */
	if (ic != 0x0)
		ic = 0x400 - ic;
	if (qc != 0x0)
		qc = 0x400 - qc;
	if (ic < 0x300) {
		ic = ic * 2 * 6 / 5;
		ic = ic + 0x80;
	} else {
		ic = (0x400 - ic) * 2 * 6 / 5;
		ic = 0x7f - ic;
	}
	if (qc < 0x300) {
		qc = qc * 2 * 6 / 5;
		qc = qc + 0x80;
	} else {
		qc = (0x400 - qc) * 2 * 6 / 5;
		qc = 0x7f - qc;
	}

	*ic_out = ic;
	*qc_out = qc;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] before i=0x%x, q=0x%x\n", ic_in, qc_in);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] after  i=0x%x, q=0x%x\n", ic, qc);
}

static void rtw8822c_dac_cal_step3(struct rtw_dev *rtwdev, u8 path,
				   u32 adc_ic, u32 adc_qc,
				   u32 *ic_in, u32 *qc_in,
				   u32 *i_out, u32 *q_out)
{
	u32 base_addr;
	u32 read_addr;
	u32 ic, qc;
	u32 temp;

	base_addr = rtw8822c_get_path_write_addr(path);
	read_addr = rtw8822c_get_path_read_addr(path);
	ic = *ic_in;
	qc = *qc_in;

	rtw_write32(rtwdev, base_addr + 0x0c, 0xdff00220);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c5);
	rtw_write32(rtwdev, 0x9b4, 0xdb66db00);
	rtw_write32(rtwdev, base_addr + 0xb0, 0x0a11fb88);
	rtw_write32(rtwdev, base_addr + 0xbc, 0xc008ff81);
	rtw_write32(rtwdev, base_addr + 0xc0, 0x0003d208);
	rtw_write32_mask(rtwdev, base_addr + 0xbc, 0xf0000000, ic & 0xf);
	rtw_write32_mask(rtwdev, base_addr + 0xc0, 0xf, (ic & 0xf0) >> 4);
	rtw_write32(rtwdev, base_addr + 0xcc, 0x0a11fb88);
	rtw_write32(rtwdev, base_addr + 0xd8, 0xe008ff81);
	rtw_write32(rtwdev, base_addr + 0xdc, 0x0003d208);
	rtw_write32_mask(rtwdev, base_addr + 0xd8, 0xf0000000, qc & 0xf);
	rtw_write32_mask(rtwdev, base_addr + 0xdc, 0xf, (qc & 0xf0) >> 4);
	rtw_write32(rtwdev, base_addr + 0xb8, 0x60000000);
	mdelay(2);
	rtw_write32_mask(rtwdev, base_addr + 0xbc, 0xe, 0x6);
	mdelay(2);
	rtw_write32(rtwdev, base_addr + 0xb0, 0x0a11fb89);
	rtw_write32(rtwdev, base_addr + 0xcc, 0x0a11fb89);
	mdelay(1);
	rtw_write32(rtwdev, base_addr + 0xb8, 0x62000000);
	rtw_write32(rtwdev, base_addr + 0xd4, 0x62000000);
	mdelay(20);
	if (!check_hw_ready(rtwdev, read_addr + 0x24, 0x07f80000, ic) ||
	    !check_hw_ready(rtwdev, read_addr + 0x50, 0x07f80000, qc))
		rtw_err(rtwdev, "failed to write IQ vector to hardware\n");
	rtw_write32(rtwdev, base_addr + 0xb8, 0x02000000);
	mdelay(1);
	rtw_write32_mask(rtwdev, base_addr + 0xbc, 0xe, 0x3);
	rtw_write32(rtwdev, 0x9b4, 0xdb6db600);

	/* check DAC DC offset */
	temp = ((adc_ic + 0x10) & 0x3ff) | (((adc_qc + 0x10) & 0x3ff) << 10);
	rtw_write32(rtwdev, base_addr + 0x68, temp);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c5);
	rtw_write32(rtwdev, base_addr + 0x60, 0xf0000000);
	rtw8822c_dac_cal_rf_mode(rtwdev, &ic, &qc);
	if (ic >= 0x10)
		ic = ic - 0x10;
	else
		ic = 0x400 - (0x10 - ic);

	if (qc >= 0x10)
		qc = qc - 0x10;
	else
		qc = 0x400 - (0x10 - qc);

	*i_out = ic;
	*q_out = qc;

	if (ic >= 0x200)
		ic = 0x400 - ic;
	if (qc >= 0x200)
		qc = 0x400 - qc;

	*ic_in = ic;
	*qc_in = qc;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[DACK] after  DACK i=0x%x, q=0x%x\n", *i_out, *q_out);
}

static void rtw8822c_dac_cal_step4(struct rtw_dev *rtwdev, u8 path)
{
	u32 base_addr = rtw8822c_get_path_write_addr(path);

	rtw_write32(rtwdev, base_addr + 0x68, 0x0);
	rtw_write32(rtwdev, base_addr + 0x10, 0x02d508c4);
	rtw_write32_mask(rtwdev, base_addr + 0xbc, 0x1, 0x0);
	rtw_write32_mask(rtwdev, base_addr + 0x30, BIT(30), 0x1);
}

static void rtw8822c_dac_cal_backup_vec(struct rtw_dev *rtwdev,
					u8 path, u8 vec, u32 w_addr, u32 r_addr)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u16 val;
	u32 i;

	if (WARN_ON(vec >= 2))
		return;

	for (i = 0; i < DACK_MSBK_BACKUP_NUM; i++) {
		rtw_write32_mask(rtwdev, w_addr, 0xf0000000, i);
		val = (u16)rtw_read32_mask(rtwdev, r_addr, 0x7fc0000);
		dm_info->dack_msbk[path][vec][i] = val;
	}
}

static void rtw8822c_dac_cal_backup_path(struct rtw_dev *rtwdev, u8 path)
{
	u32 w_off = 0x1c;
	u32 r_off = 0x2c;
	u32 w_addr, r_addr;

	if (WARN_ON(path >= 2))
		return;

	/* backup I vector */
	w_addr = rtw8822c_get_path_write_addr(path) + 0xb0;
	r_addr = rtw8822c_get_path_read_addr(path) + 0x10;
	rtw8822c_dac_cal_backup_vec(rtwdev, path, 0, w_addr, r_addr);

	/* backup Q vector */
	w_addr = rtw8822c_get_path_write_addr(path) + 0xb0 + w_off;
	r_addr = rtw8822c_get_path_read_addr(path) + 0x10 + r_off;
	rtw8822c_dac_cal_backup_vec(rtwdev, path, 1, w_addr, r_addr);
}

static void rtw8822c_dac_cal_backup_dck(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 val;

	val = (u8)rtw_read32_mask(rtwdev, REG_DCKA_I_0, 0xf0000000);
	dm_info->dack_dck[RF_PATH_A][0][0] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKA_I_1, 0xf);
	dm_info->dack_dck[RF_PATH_A][0][1] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKA_Q_0, 0xf0000000);
	dm_info->dack_dck[RF_PATH_A][1][0] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKA_Q_1, 0xf);
	dm_info->dack_dck[RF_PATH_A][1][1] = val;

	val = (u8)rtw_read32_mask(rtwdev, REG_DCKB_I_0, 0xf0000000);
	dm_info->dack_dck[RF_PATH_B][0][0] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKB_I_1, 0xf);
	dm_info->dack_dck[RF_PATH_B][1][0] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKB_Q_0, 0xf0000000);
	dm_info->dack_dck[RF_PATH_B][0][1] = val;
	val = (u8)rtw_read32_mask(rtwdev, REG_DCKB_Q_1, 0xf);
	dm_info->dack_dck[RF_PATH_B][1][1] = val;
}

static void rtw8822c_dac_cal_backup(struct rtw_dev *rtwdev)
{
	u32 temp[3];

	temp[0] = rtw_read32(rtwdev, 0x1860);
	temp[1] = rtw_read32(rtwdev, 0x4160);
	temp[2] = rtw_read32(rtwdev, 0x9b4);

	/* set clock */
	rtw_write32(rtwdev, 0x9b4, 0xdb66db00);

	/* backup path-A I/Q */
	rtw_write32_clr(rtwdev, 0x1830, BIT(30));
	rtw_write32_mask(rtwdev, 0x1860, 0xfc000000, 0x3c);
	rtw8822c_dac_cal_backup_path(rtwdev, RF_PATH_A);

	/* backup path-B I/Q */
	rtw_write32_clr(rtwdev, 0x4130, BIT(30));
	rtw_write32_mask(rtwdev, 0x4160, 0xfc000000, 0x3c);
	rtw8822c_dac_cal_backup_path(rtwdev, RF_PATH_B);

	rtw8822c_dac_cal_backup_dck(rtwdev);
	rtw_write32_set(rtwdev, 0x1830, BIT(30));
	rtw_write32_set(rtwdev, 0x4130, BIT(30));

	rtw_write32(rtwdev, 0x1860, temp[0]);
	rtw_write32(rtwdev, 0x4160, temp[1]);
	rtw_write32(rtwdev, 0x9b4, temp[2]);
}

static void rtw8822c_dac_cal_restore_dck(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 val;

	rtw_write32_set(rtwdev, REG_DCKA_I_0, BIT(19));
	val = dm_info->dack_dck[RF_PATH_A][0][0];
	rtw_write32_mask(rtwdev, REG_DCKA_I_0, 0xf0000000, val);
	val = dm_info->dack_dck[RF_PATH_A][0][1];
	rtw_write32_mask(rtwdev, REG_DCKA_I_1, 0xf, val);

	rtw_write32_set(rtwdev, REG_DCKA_Q_0, BIT(19));
	val = dm_info->dack_dck[RF_PATH_A][1][0];
	rtw_write32_mask(rtwdev, REG_DCKA_Q_0, 0xf0000000, val);
	val = dm_info->dack_dck[RF_PATH_A][1][1];
	rtw_write32_mask(rtwdev, REG_DCKA_Q_1, 0xf, val);

	rtw_write32_set(rtwdev, REG_DCKB_I_0, BIT(19));
	val = dm_info->dack_dck[RF_PATH_B][0][0];
	rtw_write32_mask(rtwdev, REG_DCKB_I_0, 0xf0000000, val);
	val = dm_info->dack_dck[RF_PATH_B][0][1];
	rtw_write32_mask(rtwdev, REG_DCKB_I_1, 0xf, val);

	rtw_write32_set(rtwdev, REG_DCKB_Q_0, BIT(19));
	val = dm_info->dack_dck[RF_PATH_B][1][0];
	rtw_write32_mask(rtwdev, REG_DCKB_Q_0, 0xf0000000, val);
	val = dm_info->dack_dck[RF_PATH_B][1][1];
	rtw_write32_mask(rtwdev, REG_DCKB_Q_1, 0xf, val);
}

static void rtw8822c_dac_cal_restore_prepare(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, 0x9b4, 0xdb66db00);

	rtw_write32_mask(rtwdev, 0x18b0, BIT(27), 0x0);
	rtw_write32_mask(rtwdev, 0x18cc, BIT(27), 0x0);
	rtw_write32_mask(rtwdev, 0x41b0, BIT(27), 0x0);
	rtw_write32_mask(rtwdev, 0x41cc, BIT(27), 0x0);

	rtw_write32_mask(rtwdev, 0x1830, BIT(30), 0x0);
	rtw_write32_mask(rtwdev, 0x1860, 0xfc000000, 0x3c);
	rtw_write32_mask(rtwdev, 0x18b4, BIT(0), 0x1);
	rtw_write32_mask(rtwdev, 0x18d0, BIT(0), 0x1);

	rtw_write32_mask(rtwdev, 0x4130, BIT(30), 0x0);
	rtw_write32_mask(rtwdev, 0x4160, 0xfc000000, 0x3c);
	rtw_write32_mask(rtwdev, 0x41b4, BIT(0), 0x1);
	rtw_write32_mask(rtwdev, 0x41d0, BIT(0), 0x1);

	rtw_write32_mask(rtwdev, 0x18b0, 0xf00, 0x0);
	rtw_write32_mask(rtwdev, 0x18c0, BIT(14), 0x0);
	rtw_write32_mask(rtwdev, 0x18cc, 0xf00, 0x0);
	rtw_write32_mask(rtwdev, 0x18dc, BIT(14), 0x0);

	rtw_write32_mask(rtwdev, 0x18b0, BIT(0), 0x0);
	rtw_write32_mask(rtwdev, 0x18cc, BIT(0), 0x0);
	rtw_write32_mask(rtwdev, 0x18b0, BIT(0), 0x1);
	rtw_write32_mask(rtwdev, 0x18cc, BIT(0), 0x1);

	rtw8822c_dac_cal_restore_dck(rtwdev);

	rtw_write32_mask(rtwdev, 0x18c0, 0x38000, 0x7);
	rtw_write32_mask(rtwdev, 0x18dc, 0x38000, 0x7);
	rtw_write32_mask(rtwdev, 0x41c0, 0x38000, 0x7);
	rtw_write32_mask(rtwdev, 0x41dc, 0x38000, 0x7);

	rtw_write32_mask(rtwdev, 0x18b8, BIT(26) | BIT(25), 0x1);
	rtw_write32_mask(rtwdev, 0x18d4, BIT(26) | BIT(25), 0x1);

	rtw_write32_mask(rtwdev, 0x41b0, 0xf00, 0x0);
	rtw_write32_mask(rtwdev, 0x41c0, BIT(14), 0x0);
	rtw_write32_mask(rtwdev, 0x41cc, 0xf00, 0x0);
	rtw_write32_mask(rtwdev, 0x41dc, BIT(14), 0x0);

	rtw_write32_mask(rtwdev, 0x41b0, BIT(0), 0x0);
	rtw_write32_mask(rtwdev, 0x41cc, BIT(0), 0x0);
	rtw_write32_mask(rtwdev, 0x41b0, BIT(0), 0x1);
	rtw_write32_mask(rtwdev, 0x41cc, BIT(0), 0x1);

	rtw_write32_mask(rtwdev, 0x41b8, BIT(26) | BIT(25), 0x1);
	rtw_write32_mask(rtwdev, 0x41d4, BIT(26) | BIT(25), 0x1);
}

static bool rtw8822c_dac_cal_restore_wait(struct rtw_dev *rtwdev,
					  u32 target_addr, u32 toggle_addr)
{
	u32 cnt = 0;

	do {
		rtw_write32_mask(rtwdev, toggle_addr, BIT(26) | BIT(25), 0x0);
		rtw_write32_mask(rtwdev, toggle_addr, BIT(26) | BIT(25), 0x2);

		if (rtw_read32_mask(rtwdev, target_addr, 0xf) == 0x6)
			return true;

	} while (cnt++ < 100);

	return false;
}

static bool rtw8822c_dac_cal_restore_path(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 w_off = 0x1c;
	u32 r_off = 0x2c;
	u32 w_i, r_i, w_q, r_q;
	u32 value;
	u32 i;

	w_i = rtw8822c_get_path_write_addr(path) + 0xb0;
	r_i = rtw8822c_get_path_read_addr(path) + 0x08;
	w_q = rtw8822c_get_path_write_addr(path) + 0xb0 + w_off;
	r_q = rtw8822c_get_path_read_addr(path) + 0x08 + r_off;

	if (!rtw8822c_dac_cal_restore_wait(rtwdev, r_i, w_i + 0x8))
		return false;

	for (i = 0; i < DACK_MSBK_BACKUP_NUM; i++) {
		rtw_write32_mask(rtwdev, w_i + 0x4, BIT(2), 0x0);
		value = dm_info->dack_msbk[path][0][i];
		rtw_write32_mask(rtwdev, w_i + 0x4, 0xff8, value);
		rtw_write32_mask(rtwdev, w_i, 0xf0000000, i);
		rtw_write32_mask(rtwdev, w_i + 0x4, BIT(2), 0x1);
	}

	rtw_write32_mask(rtwdev, w_i + 0x4, BIT(2), 0x0);

	if (!rtw8822c_dac_cal_restore_wait(rtwdev, r_q, w_q + 0x8))
		return false;

	for (i = 0; i < DACK_MSBK_BACKUP_NUM; i++) {
		rtw_write32_mask(rtwdev, w_q + 0x4, BIT(2), 0x0);
		value = dm_info->dack_msbk[path][1][i];
		rtw_write32_mask(rtwdev, w_q + 0x4, 0xff8, value);
		rtw_write32_mask(rtwdev, w_q, 0xf0000000, i);
		rtw_write32_mask(rtwdev, w_q + 0x4, BIT(2), 0x1);
	}
	rtw_write32_mask(rtwdev, w_q + 0x4, BIT(2), 0x0);

	rtw_write32_mask(rtwdev, w_i + 0x8, BIT(26) | BIT(25), 0x0);
	rtw_write32_mask(rtwdev, w_q + 0x8, BIT(26) | BIT(25), 0x0);
	rtw_write32_mask(rtwdev, w_i + 0x4, BIT(0), 0x0);
	rtw_write32_mask(rtwdev, w_q + 0x4, BIT(0), 0x0);

	return true;
}

static bool __rtw8822c_dac_cal_restore(struct rtw_dev *rtwdev)
{
	if (!rtw8822c_dac_cal_restore_path(rtwdev, RF_PATH_A))
		return false;

	if (!rtw8822c_dac_cal_restore_path(rtwdev, RF_PATH_B))
		return false;

	return true;
}

static bool rtw8822c_dac_cal_restore(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 temp[3];

	/* sample the first element for both path's IQ vector */
	if (dm_info->dack_msbk[RF_PATH_A][0][0] == 0 &&
	    dm_info->dack_msbk[RF_PATH_A][1][0] == 0 &&
	    dm_info->dack_msbk[RF_PATH_B][0][0] == 0 &&
	    dm_info->dack_msbk[RF_PATH_B][1][0] == 0)
		return false;

	temp[0] = rtw_read32(rtwdev, 0x1860);
	temp[1] = rtw_read32(rtwdev, 0x4160);
	temp[2] = rtw_read32(rtwdev, 0x9b4);

	rtw8822c_dac_cal_restore_prepare(rtwdev);
	if (!check_hw_ready(rtwdev, 0x2808, 0x7fff80, 0xffff) ||
	    !check_hw_ready(rtwdev, 0x2834, 0x7fff80, 0xffff) ||
	    !check_hw_ready(rtwdev, 0x4508, 0x7fff80, 0xffff) ||
	    !check_hw_ready(rtwdev, 0x4534, 0x7fff80, 0xffff))
		return false;

	if (!__rtw8822c_dac_cal_restore(rtwdev)) {
		rtw_err(rtwdev, "failed to restore dack vectors\n");
		return false;
	}

	rtw_write32_mask(rtwdev, 0x1830, BIT(30), 0x1);
	rtw_write32_mask(rtwdev, 0x4130, BIT(30), 0x1);
	rtw_write32(rtwdev, 0x1860, temp[0]);
	rtw_write32(rtwdev, 0x4160, temp[1]);
	rtw_write32_mask(rtwdev, 0x18b0, BIT(27), 0x1);
	rtw_write32_mask(rtwdev, 0x18cc, BIT(27), 0x1);
	rtw_write32_mask(rtwdev, 0x41b0, BIT(27), 0x1);
	rtw_write32_mask(rtwdev, 0x41cc, BIT(27), 0x1);
	rtw_write32(rtwdev, 0x9b4, temp[2]);

	return true;
}

static void rtw8822c_rf_dac_cal(struct rtw_dev *rtwdev)
{
	struct rtw_backup_info backup_rf[DACK_RF_8822C * DACK_PATH_8822C];
	struct rtw_backup_info backup[DACK_REG_8822C];
	u32 ic = 0, qc = 0, i;
	u32 i_a = 0x0, q_a = 0x0, i_b = 0x0, q_b = 0x0;
	u32 ic_a = 0x0, qc_a = 0x0, ic_b = 0x0, qc_b = 0x0;
	u32 adc_ic_a = 0x0, adc_qc_a = 0x0, adc_ic_b = 0x0, adc_qc_b = 0x0;

	if (rtw8822c_dac_cal_restore(rtwdev))
		return;

	/* not able to restore, do it */

	rtw8822c_dac_backup_reg(rtwdev, backup, backup_rf);

	rtw8822c_dac_bb_setting(rtwdev);

	/* path-A */
	rtw8822c_dac_cal_adc(rtwdev, RF_PATH_A, &adc_ic_a, &adc_qc_a);
	for (i = 0; i < 10; i++) {
		rtw8822c_dac_cal_step1(rtwdev, RF_PATH_A);
		rtw8822c_dac_cal_step2(rtwdev, RF_PATH_A, &ic, &qc);
		ic_a = ic;
		qc_a = qc;

		rtw8822c_dac_cal_step3(rtwdev, RF_PATH_A, adc_ic_a, adc_qc_a,
				       &ic, &qc, &i_a, &q_a);

		if (ic < 5 && qc < 5)
			break;
	}
	rtw8822c_dac_cal_step4(rtwdev, RF_PATH_A);

	/* path-B */
	rtw8822c_dac_cal_adc(rtwdev, RF_PATH_B, &adc_ic_b, &adc_qc_b);
	for (i = 0; i < 10; i++) {
		rtw8822c_dac_cal_step1(rtwdev, RF_PATH_B);
		rtw8822c_dac_cal_step2(rtwdev, RF_PATH_B, &ic, &qc);
		ic_b = ic;
		qc_b = qc;

		rtw8822c_dac_cal_step3(rtwdev, RF_PATH_B, adc_ic_b, adc_qc_b,
				       &ic, &qc, &i_b, &q_b);

		if (ic < 5 && qc < 5)
			break;
	}
	rtw8822c_dac_cal_step4(rtwdev, RF_PATH_B);

	rtw_write32(rtwdev, 0x1b00, 0x00000008);
	rtw_write32_mask(rtwdev, 0x4130, BIT(30), 0x1);
	rtw_write8(rtwdev, 0x1bcc, 0x0);
	rtw_write32(rtwdev, 0x1b00, 0x0000000a);
	rtw_write8(rtwdev, 0x1bcc, 0x0);

	rtw8822c_dac_restore_reg(rtwdev, backup, backup_rf);

	/* backup results to restore, saving a lot of time */
	rtw8822c_dac_cal_backup(rtwdev);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] path A: ic=0x%x, qc=0x%x\n", ic_a, qc_a);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] path B: ic=0x%x, qc=0x%x\n", ic_b, qc_b);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] path A: i=0x%x, q=0x%x\n", i_a, q_a);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DACK] path B: i=0x%x, q=0x%x\n", i_b, q_b);
}

static void rtw8822c_rf_x2_check(struct rtw_dev *rtwdev)
{
	u8 x2k_busy;

	mdelay(1);
	x2k_busy = rtw_read_rf(rtwdev, RF_PATH_A, 0xb8, BIT(15));
	if (x2k_busy == 1) {
		rtw_write_rf(rtwdev, RF_PATH_A, 0xb8, RFREG_MASK, 0xC4440);
		rtw_write_rf(rtwdev, RF_PATH_A, 0xba, RFREG_MASK, 0x6840D);
		rtw_write_rf(rtwdev, RF_PATH_A, 0xb8, RFREG_MASK, 0x80440);
		mdelay(1);
	}
}

static void rtw8822c_set_power_trim(struct rtw_dev *rtwdev, s8 bb_gain[2][8])
{
#define RF_SET_POWER_TRIM(_path, _seq, _idx)					\
		do {								\
			rtw_write_rf(rtwdev, _path, 0x33, RFREG_MASK, _seq);	\
			rtw_write_rf(rtwdev, _path, 0x3f, RFREG_MASK,		\
				     bb_gain[_path][_idx]);			\
		} while (0)
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write_rf(rtwdev, path, 0xee, BIT(19), 1);
		RF_SET_POWER_TRIM(path, 0x0, 0);
		RF_SET_POWER_TRIM(path, 0x1, 1);
		RF_SET_POWER_TRIM(path, 0x2, 2);
		RF_SET_POWER_TRIM(path, 0x3, 2);
		RF_SET_POWER_TRIM(path, 0x4, 3);
		RF_SET_POWER_TRIM(path, 0x5, 4);
		RF_SET_POWER_TRIM(path, 0x6, 5);
		RF_SET_POWER_TRIM(path, 0x7, 6);
		RF_SET_POWER_TRIM(path, 0x8, 7);
		RF_SET_POWER_TRIM(path, 0x9, 3);
		RF_SET_POWER_TRIM(path, 0xa, 4);
		RF_SET_POWER_TRIM(path, 0xb, 5);
		RF_SET_POWER_TRIM(path, 0xc, 6);
		RF_SET_POWER_TRIM(path, 0xd, 7);
		RF_SET_POWER_TRIM(path, 0xe, 7);
		rtw_write_rf(rtwdev, path, 0xee, BIT(19), 0);
	}
#undef RF_SET_POWER_TRIM
}

static void rtw8822c_power_trim(struct rtw_dev *rtwdev)
{
	u8 pg_pwr = 0xff, i, path, idx;
	s8 bb_gain[2][8] = {};
	u16 rf_efuse_2g[3] = {PPG_2GL_TXAB, PPG_2GM_TXAB, PPG_2GH_TXAB};
	u16 rf_efuse_5g[2][5] = {{PPG_5GL1_TXA, PPG_5GL2_TXA, PPG_5GM1_TXA,
				  PPG_5GM2_TXA, PPG_5GH1_TXA},
				 {PPG_5GL1_TXB, PPG_5GL2_TXB, PPG_5GM1_TXB,
				  PPG_5GM2_TXB, PPG_5GH1_TXB} };
	bool set = false;

	for (i = 0; i < ARRAY_SIZE(rf_efuse_2g); i++) {
		rtw_read8_physical_efuse(rtwdev, rf_efuse_2g[i], &pg_pwr);
		if (pg_pwr == EFUSE_READ_FAIL)
			continue;
		set = true;
		bb_gain[RF_PATH_A][i] = FIELD_GET(PPG_2G_A_MASK, pg_pwr);
		bb_gain[RF_PATH_B][i] = FIELD_GET(PPG_2G_B_MASK, pg_pwr);
	}

	for (i = 0; i < ARRAY_SIZE(rf_efuse_5g[0]); i++) {
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			rtw_read8_physical_efuse(rtwdev, rf_efuse_5g[path][i],
						 &pg_pwr);
			if (pg_pwr == EFUSE_READ_FAIL)
				continue;
			set = true;
			idx = i + ARRAY_SIZE(rf_efuse_2g);
			bb_gain[path][idx] = FIELD_GET(PPG_5G_MASK, pg_pwr);
		}
	}
	if (set)
		rtw8822c_set_power_trim(rtwdev, bb_gain);

	rtw_write32_mask(rtwdev, REG_DIS_DPD, DIS_DPD_MASK, DIS_DPD_RATEALL);
}

static void rtw8822c_thermal_trim(struct rtw_dev *rtwdev)
{
	u16 rf_efuse[2] = {PPG_THERMAL_A, PPG_THERMAL_B};
	u8 pg_therm = 0xff, thermal[2] = {0}, path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_read8_physical_efuse(rtwdev, rf_efuse[path], &pg_therm);
		if (pg_therm == EFUSE_READ_FAIL)
			return;
		/* Efuse value of BIT(0) shall be move to BIT(3), and the value
		 * of BIT(1) to BIT(3) should be right shifted 1 bit.
		 */
		thermal[path] = FIELD_GET(GENMASK(3, 1), pg_therm);
		thermal[path] |= FIELD_PREP(BIT(3), pg_therm & BIT(0));
		rtw_write_rf(rtwdev, path, 0x43, RF_THEMAL_MASK, thermal[path]);
	}
}

static void rtw8822c_pa_bias(struct rtw_dev *rtwdev)
{
	u16 rf_efuse_2g[2] = {PPG_PABIAS_2GA, PPG_PABIAS_2GB};
	u16 rf_efuse_5g[2] = {PPG_PABIAS_5GA, PPG_PABIAS_5GB};
	u8 pg_pa_bias = 0xff, path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_read8_physical_efuse(rtwdev, rf_efuse_2g[path],
					 &pg_pa_bias);
		if (pg_pa_bias == EFUSE_READ_FAIL)
			return;
		pg_pa_bias = FIELD_GET(PPG_PABIAS_MASK, pg_pa_bias);
		rtw_write_rf(rtwdev, path, RF_PA, RF_PABIAS_2G_MASK, pg_pa_bias);
	}
	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_read8_physical_efuse(rtwdev, rf_efuse_5g[path],
					 &pg_pa_bias);
		pg_pa_bias = FIELD_GET(PPG_PABIAS_MASK, pg_pa_bias);
		rtw_write_rf(rtwdev, path, RF_PA, RF_PABIAS_5G_MASK, pg_pa_bias);
	}
}

static void rtw8822c_rfk_handshake(struct rtw_dev *rtwdev, bool is_before_k)
{
	struct rtw_dm_info *dm = &rtwdev->dm_info;
	u8 u1b_tmp;
	u8 u4b_tmp;
	int ret;

	if (is_before_k) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[RFK] WiFi / BT RFK handshake start!!\n");

		if (!dm->is_bt_iqk_timeout) {
			ret = read_poll_timeout(rtw_read32_mask, u4b_tmp,
						u4b_tmp == 0, 20, 600000, false,
						rtwdev, REG_PMC_DBG_CTRL1,
						BITS_PMC_BT_IQK_STS);
			if (ret) {
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[RFK] Wait BT IQK finish timeout!!\n");
				dm->is_bt_iqk_timeout = true;
			}
		}

		rtw_fw_inform_rfk_status(rtwdev, true);

		ret = read_poll_timeout(rtw_read8_mask, u1b_tmp,
					u1b_tmp == 1, 20, 100000, false,
					rtwdev, REG_ARFR4, BIT_WL_RFK);
		if (ret)
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[RFK] Send WiFi RFK start H2C cmd FAIL!!\n");
	} else {
		rtw_fw_inform_rfk_status(rtwdev, false);
		ret = read_poll_timeout(rtw_read8_mask, u1b_tmp,
					u1b_tmp == 1, 20, 100000, false,
					rtwdev, REG_ARFR4,
					BIT_WL_RFK);
		if (ret)
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[RFK] Send WiFi RFK finish H2C cmd FAIL!!\n");

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[RFK] WiFi / BT RFK handshake finish!!\n");
	}
}

static void rtw8822c_rfk_power_save(struct rtw_dev *rtwdev,
				    bool is_power_save)
{
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_PS_EN,
				 is_power_save ? 0 : 1);
	}
}

static void rtw8822c_txgapk_backup_bb_reg(struct rtw_dev *rtwdev, const u32 reg[],
					  u32 reg_backup[], u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		reg_backup[i] = rtw_read32(rtwdev, reg[i]);

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] Backup BB 0x%x = 0x%x\n",
			reg[i], reg_backup[i]);
	}
}

static void rtw8822c_txgapk_reload_bb_reg(struct rtw_dev *rtwdev,
					  const u32 reg[], u32 reg_backup[],
					  u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		rtw_write32(rtwdev, reg[i], reg_backup[i]);
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] Reload BB 0x%x = 0x%x\n",
			reg[i], reg_backup[i]);
	}
}

static bool check_rf_status(struct rtw_dev *rtwdev, u8 status)
{
	u8 reg_rf0_a, reg_rf0_b;

	reg_rf0_a = (u8)rtw_read_rf(rtwdev, RF_PATH_A,
				    RF_MODE_TRXAGC, BIT_RF_MODE);
	reg_rf0_b = (u8)rtw_read_rf(rtwdev, RF_PATH_B,
				    RF_MODE_TRXAGC, BIT_RF_MODE);

	if (reg_rf0_a == status || reg_rf0_b == status)
		return false;

	return true;
}

static void rtw8822c_txgapk_tx_pause(struct rtw_dev *rtwdev)
{
	bool status;
	int ret;

	rtw_write8(rtwdev, REG_TXPAUSE, BIT_AC_QUEUE);
	rtw_write32_mask(rtwdev, REG_TX_FIFO, BIT_STOP_TX, 0x2);

	ret = read_poll_timeout_atomic(check_rf_status, status, status,
				       2, 5000, false, rtwdev, 2);
	if (ret)
		rtw_warn(rtwdev, "failed to pause TX\n");

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] Tx pause!!\n");
}

static void rtw8822c_txgapk_bb_dpk(struct rtw_dev *rtwdev, u8 path)
{
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	rtw_write32_mask(rtwdev, REG_ENFN, BIT_IQK_DPK_EN, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2,
			 BIT_IQK_DPK_CLOCK_SRC, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2,
			 BIT_IQK_DPK_RESET_SRC, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2, BIT_EN_IOQ_IQK_DPK, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2, BIT_TST_IQK2SET_SRC, 0x0);
	rtw_write32_mask(rtwdev, REG_CCA_OFF, BIT_CCA_ON_BY_PW, 0x1ff);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_A,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_DIS_SHARERX_TXGAT, 0x1);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_A,
				 BIT_TX_SCALE_0DB, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_3WIRE_EN, 0x0);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_B,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE2,
				 BIT_DIS_SHARERX_TXGAT, 0x1);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_B,
				 BIT_TX_SCALE_0DB, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE2, BIT_3WIRE_EN, 0x0);
	}
	rtw_write32_mask(rtwdev, REG_CCKSB, BIT_BBMODE, 0x2);
}

static void rtw8822c_txgapk_afe_dpk(struct rtw_dev *rtwdev, u8 path)
{
	u32 reg;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (path == RF_PATH_A) {
		reg = REG_ANAPAR_A;
	} else if (path == RF_PATH_B) {
		reg = REG_ANAPAR_B;
	} else {
		rtw_err(rtwdev, "[TXGAPK] unknown path %d!!\n", path);
		return;
	}

	rtw_write32_mask(rtwdev, REG_IQK_CTRL, MASKDWORD, MASKDWORD);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x701f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x702f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x703f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x704f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x705f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x706f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x707f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x708f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x709f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70af0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70bf0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70cf0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70df0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ef0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ff0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ff0001);
}

static void rtw8822c_txgapk_afe_dpk_restore(struct rtw_dev *rtwdev, u8 path)
{
	u32 reg;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (path == RF_PATH_A) {
		reg = REG_ANAPAR_A;
	} else if (path == RF_PATH_B) {
		reg = REG_ANAPAR_B;
	} else {
		rtw_err(rtwdev, "[TXGAPK] unknown path %d!!\n", path);
		return;
	}
	rtw_write32_mask(rtwdev, REG_IQK_CTRL, MASKDWORD, 0xffa1005e);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70144041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70244041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70344041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70444041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x705b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70644041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x707b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x708b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x709b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ab8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70bb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70cb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70db8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70eb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70fb8041);
}

static void rtw8822c_txgapk_bb_dpk_restore(struct rtw_dev *rtwdev, u8 path)
{
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x0);
	rtw_write_rf(rtwdev, path, RF_DIS_BYPASS_TXBB, BIT_TIA_BYPASS, 0x0);
	rtw_write_rf(rtwdev, path, RF_DIS_BYPASS_TXBB, BIT_TXBB, 0x0);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, 0x00);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x1);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, 0x00);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x0);
	rtw_write32_mask(rtwdev, REG_CCA_OFF, BIT_CCA_ON_BY_PW, 0x0);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_A,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_DIS_SHARERX_TXGAT, 0x0);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_A,
				 BIT_TX_SCALE_0DB, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_3WIRE_EN, 0x3);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_B,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE2,
				 BIT_DIS_SHARERX_TXGAT, 0x0);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_B,
				 BIT_TX_SCALE_0DB, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE2, BIT_3WIRE_EN, 0x3);
	}

	rtw_write32_mask(rtwdev, REG_CCKSB, BIT_BBMODE, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_CFIR_EN, 0x5);
}

static bool _rtw8822c_txgapk_gain_valid(struct rtw_dev *rtwdev, u32 gain)
{
	if ((FIELD_GET(BIT_GAIN_TX_PAD_H, gain) >= 0xc) &&
	    (FIELD_GET(BIT_GAIN_TX_PAD_L, gain) >= 0xe))
		return true;

	return false;
}

static void _rtw8822c_txgapk_write_gain_bb_table(struct rtw_dev *rtwdev,
						 u8 band, u8 path)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u32 v, tmp_3f = 0;
	u8 gain, check_txgain;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);

	switch (band) {
	case RF_BAND_2G_OFDM:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x0);
		break;
	case RF_BAND_5G_L:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x2);
		break;
	case RF_BAND_5G_M:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x3);
		break;
	case RF_BAND_5G_H:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x4);
		break;
	default:
		break;
	}

	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, MASKBYTE0, 0x88);

	check_txgain = 0;
	for (gain = 0; gain < RF_GAIN_NUM; gain++) {
		v = txgapk->rf3f_bp[band][gain][path];
		if (_rtw8822c_txgapk_gain_valid(rtwdev, v)) {
			if (!check_txgain) {
				tmp_3f = txgapk->rf3f_bp[band][gain][path];
				check_txgain = 1;
			}
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[TXGAPK] tx_gain=0x%03X >= 0xCEX\n",
				txgapk->rf3f_bp[band][gain][path]);
		} else {
			tmp_3f = txgapk->rf3f_bp[band][gain][path];
		}

		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN, tmp_3f);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_I_GAIN, gain);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_GAIN_RST, 0x1);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_GAIN_RST, 0x0);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] Band=%d 0x1b98[11:0]=0x%03X path=%d\n",
			band, tmp_3f, path);
	}
}

static void rtw8822c_txgapk_write_gain_bb_table(struct rtw_dev *rtwdev)
{
	u8 path, band;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s channel=%d\n",
		__func__, rtwdev->dm_info.gapk.channel);

	for (band = 0; band < RF_BAND_MAX; band++) {
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			_rtw8822c_txgapk_write_gain_bb_table(rtwdev,
							     band, path);
		}
	}
}

static void rtw8822c_txgapk_read_offset(struct rtw_dev *rtwdev, u8 path)
{
	static const u32 cfg1_1b00[2] = {0x00000d18, 0x00000d2a};
	static const u32 cfg2_1b00[2] = {0x00000d19, 0x00000d2b};
	static const u32 set_pi[2] = {REG_RSV_CTRL, REG_WLRF1};
	static const u32 path_setting[2] = {REG_ORITXCODE, REG_ORITXCODE2};
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u8 channel = txgapk->channel;
	u32 val;
	int i;

	if (path >= ARRAY_SIZE(cfg1_1b00) ||
	    path >= ARRAY_SIZE(cfg2_1b00) ||
	    path >= ARRAY_SIZE(set_pi) ||
	    path >= ARRAY_SIZE(path_setting)) {
		rtw_warn(rtwdev, "[TXGAPK] wrong path %d\n", path);
		return;
	}

	rtw_write32_mask(rtwdev, REG_ANTMAP0, BIT_ANT_PATH, path + 1);
	rtw_write32_mask(rtwdev, REG_TXLGMAP, MASKDWORD, 0xe4e40000);
	rtw_write32_mask(rtwdev, REG_TXANTSEG, BIT_ANTSEG, 0x3);
	rtw_write32_mask(rtwdev, path_setting[path], MASK20BITS, 0x33312);
	rtw_write32_mask(rtwdev, path_setting[path], BIT_PATH_EN, 0x1);
	rtw_write32_mask(rtwdev, set_pi[path], BITS_RFC_DIRECT, 0x0);
	rtw_write_rf(rtwdev, path, RF_LUTDBG, BIT_TXA_TANK, 0x1);
	rtw_write_rf(rtwdev, path, RF_IDAC, BIT_TX_MODE, 0x820);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
	rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x0);

	rtw_write32_mask(rtwdev, REG_TX_TONE_IDX, MASKBYTE0, 0x018);
	fsleep(1000);
	if (channel >= 1 && channel <= 14)
		rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, BIT_2G_SWING);
	else
		rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, BIT_5G_SWING);
	fsleep(1000);

	rtw_write32_mask(rtwdev, REG_NCTL0, MASKDWORD, cfg1_1b00[path]);
	rtw_write32_mask(rtwdev, REG_NCTL0, MASKDWORD, cfg2_1b00[path]);

	read_poll_timeout(rtw_read32_mask, val,
			  val == 0x55, 1000, 100000, false,
			  rtwdev, REG_RPT_CIP, BIT_RPT_CIP_STATUS);

	rtw_write32_mask(rtwdev, set_pi[path], BITS_RFC_DIRECT, 0x2);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_EN, 0x1);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x12);
	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, BIT_GAPK_RPT_IDX, 0x3);
	val = rtw_read32(rtwdev, REG_STAT_RPT);

	txgapk->offset[0][path] = (s8)FIELD_GET(BIT_GAPK_RPT0, val);
	txgapk->offset[1][path] = (s8)FIELD_GET(BIT_GAPK_RPT1, val);
	txgapk->offset[2][path] = (s8)FIELD_GET(BIT_GAPK_RPT2, val);
	txgapk->offset[3][path] = (s8)FIELD_GET(BIT_GAPK_RPT3, val);
	txgapk->offset[4][path] = (s8)FIELD_GET(BIT_GAPK_RPT4, val);
	txgapk->offset[5][path] = (s8)FIELD_GET(BIT_GAPK_RPT5, val);
	txgapk->offset[6][path] = (s8)FIELD_GET(BIT_GAPK_RPT6, val);
	txgapk->offset[7][path] = (s8)FIELD_GET(BIT_GAPK_RPT7, val);

	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, BIT_GAPK_RPT_IDX, 0x4);
	val = rtw_read32(rtwdev, REG_STAT_RPT);

	txgapk->offset[8][path] = (s8)FIELD_GET(BIT_GAPK_RPT0, val);
	txgapk->offset[9][path] = (s8)FIELD_GET(BIT_GAPK_RPT1, val);

	for (i = 0; i < RF_HW_OFFSET_NUM; i++)
		if (txgapk->offset[i][path] & BIT(3))
			txgapk->offset[i][path] = txgapk->offset[i][path] |
						  0xf0;
	for (i = 0; i < RF_HW_OFFSET_NUM; i++)
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] offset %d %d path=%d\n",
			txgapk->offset[i][path], i, path);
}

static void rtw8822c_txgapk_calculate_offset(struct rtw_dev *rtwdev, u8 path)
{
	static const u32 bb_reg[] = {REG_ANTMAP0, REG_TXLGMAP, REG_TXANTSEG,
				     REG_ORITXCODE, REG_ORITXCODE2};
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u8 channel = txgapk->channel;
	u32 reg_backup[ARRAY_SIZE(bb_reg)] = {0};

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s channel=%d\n",
		__func__, channel);

	rtw8822c_txgapk_backup_bb_reg(rtwdev, bb_reg,
				      reg_backup, ARRAY_SIZE(bb_reg));

	if (channel >= 1 && channel <= 14) {
		rtw_write32_mask(rtwdev,
				 REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x3f);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x5000f);
		rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_RF_GAIN, 0x0);
		rtw_write_rf(rtwdev, path, RF_RXG_GAIN, BIT_RXG_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0x0f);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);
		rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x1);

		rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x00);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x0);

		rtw8822c_txgapk_read_offset(rtwdev, path);
		rtw_dbg(rtwdev, RTW_DBG_RFK, "=============================\n");

	} else {
		rtw_write32_mask(rtwdev,
				 REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x3f);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x50011);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_TXA_LB_ATT, 0x3);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_ATT, 0x3);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_SW, 0x1);
		rtw_write_rf(rtwdev, path,
			     RF_RXA_MIX_GAIN, BIT_RXA_MIX_GAIN, 0x2);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0x12);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);
		rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RF_MODE, 0x5);

		rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x0);

		if (channel >= 36 && channel <= 64)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x2);
		else if (channel >= 100 && channel <= 144)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x3);
		else if (channel >= 149 && channel <= 177)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x4);

		rtw8822c_txgapk_read_offset(rtwdev, path);
		rtw_dbg(rtwdev, RTW_DBG_RFK, "=============================\n");
	}
	rtw8822c_txgapk_reload_bb_reg(rtwdev, bb_reg,
				      reg_backup, ARRAY_SIZE(bb_reg));
}

static void rtw8822c_txgapk_rf_restore(struct rtw_dev *rtwdev, u8 path)
{
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (path >= rtwdev->hal.rf_path_num)
		return;

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RF_MODE, 0x3);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x0);
	rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x0);
}

static u32 rtw8822c_txgapk_cal_gain(struct rtw_dev *rtwdev, u32 gain, s8 offset)
{
	u32 gain_x2, new_gain;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (_rtw8822c_txgapk_gain_valid(rtwdev, gain)) {
		new_gain = gain;
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] gain=0x%03X(>=0xCEX) offset=%d new_gain=0x%03X\n",
			gain, offset, new_gain);
		return new_gain;
	}

	gain_x2 = (gain << 1) + offset;
	new_gain = (gain_x2 >> 1) | (gain_x2 & BIT(0) ? BIT_GAIN_EXT : 0);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[TXGAPK] gain=0x%X offset=%d new_gain=0x%X\n",
		gain, offset, new_gain);

	return new_gain;
}

static void rtw8822c_txgapk_write_tx_gain(struct rtw_dev *rtwdev)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u32 i, j, tmp = 0x20, tmp_3f, v;
	s8 offset_tmp[RF_GAIN_NUM] = {0};
	u8 path, band = RF_BAND_2G_OFDM, channel = txgapk->channel;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (channel >= 1 && channel <= 14) {
		tmp = 0x20;
		band = RF_BAND_2G_OFDM;
	} else if (channel >= 36 && channel <= 64) {
		tmp = 0x200;
		band = RF_BAND_5G_L;
	} else if (channel >= 100 && channel <= 144) {
		tmp = 0x280;
		band = RF_BAND_5G_M;
	} else if (channel >= 149 && channel <= 177) {
		tmp = 0x300;
		band = RF_BAND_5G_H;
	} else {
		rtw_err(rtwdev, "[TXGAPK] unknown channel %d!!\n", channel);
		return;
	}

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		for (i = 0; i < RF_GAIN_NUM; i++) {
			offset_tmp[i] = 0;
			for (j = i; j < RF_GAIN_NUM; j++) {
				v = txgapk->rf3f_bp[band][j][path];
				if (_rtw8822c_txgapk_gain_valid(rtwdev, v))
					continue;

				offset_tmp[i] += txgapk->offset[j][path];
				txgapk->fianl_offset[i][path] = offset_tmp[i];
			}

			v = txgapk->rf3f_bp[band][i][path];
			if (_rtw8822c_txgapk_gain_valid(rtwdev, v)) {
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[TXGAPK] tx_gain=0x%03X >= 0xCEX\n",
					txgapk->rf3f_bp[band][i][path]);
			} else {
				txgapk->rf3f_fs[path][i] = offset_tmp[i];
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[TXGAPK] offset %d %d\n",
					offset_tmp[i], i);
			}
		}

		rtw_write_rf(rtwdev, path, RF_LUTWE2, RFREG_MASK, 0x10000);
		for (i = 0; i < RF_GAIN_NUM; i++) {
			rtw_write_rf(rtwdev, path,
				     RF_LUTWA, RFREG_MASK, tmp + i);

			tmp_3f = rtw8822c_txgapk_cal_gain(rtwdev,
							  txgapk->rf3f_bp[band][i][path],
							  offset_tmp[i]);
			rtw_write_rf(rtwdev, path, RF_LUTWD0,
				     BIT_GAIN_EXT | BIT_DATA_L, tmp_3f);

			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[TXGAPK] 0x33=0x%05X 0x3f=0x%04X\n",
				tmp + i, tmp_3f);
		}
		rtw_write_rf(rtwdev, path, RF_LUTWE2, RFREG_MASK, 0x0);
	}
}

static void rtw8822c_txgapk_save_all_tx_gain_table(struct rtw_dev *rtwdev)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	static const u32 three_wire[2] = {REG_3WIRE, REG_3WIRE2};
	static const u8 ch_num[RF_BAND_MAX] = {1, 1, 36, 100, 149};
	static const u8 band_num[RF_BAND_MAX] = {0x0, 0x0, 0x1, 0x3, 0x5};
	static const u8 cck[RF_BAND_MAX] = {0x1, 0x0, 0x0, 0x0, 0x0};
	u8 path, band, gain, rf0_idx;
	u32 rf18, v;

	if (rtwdev->dm_info.dm_flags & BIT(RTW_DM_CAP_TXGAPK))
		return;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	if (txgapk->read_txgain == 1) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] Already Read txgapk->read_txgain return!!!\n");
		rtw8822c_txgapk_write_gain_bb_table(rtwdev);
		return;
	}

	for (band = 0; band < RF_BAND_MAX; band++) {
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			rf18 = rtw_read_rf(rtwdev, path, RF_CFGCH, RFREG_MASK);

			rtw_write32_mask(rtwdev,
					 three_wire[path], BIT_3WIRE_EN, 0x0);
			rtw_write_rf(rtwdev, path,
				     RF_CFGCH, MASKBYTE0, ch_num[band]);
			rtw_write_rf(rtwdev, path,
				     RF_CFGCH, BIT_BAND, band_num[band]);
			rtw_write_rf(rtwdev, path,
				     RF_BW_TRXBB, BIT_DBG_CCK_CCA, cck[band]);
			rtw_write_rf(rtwdev, path,
				     RF_BW_TRXBB, BIT_TX_CCK_IND, cck[band]);
			gain = 0;
			for (rf0_idx = 1; rf0_idx < 32; rf0_idx += 3) {
				rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC,
					     MASKBYTE0, rf0_idx);
				v = rtw_read_rf(rtwdev, path,
						RF_TX_RESULT, RFREG_MASK);
				txgapk->rf3f_bp[band][gain][path] = v & BIT_DATA_L;

				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[TXGAPK] 0x5f=0x%03X band=%d path=%d\n",
					txgapk->rf3f_bp[band][gain][path],
					band, path);
				gain++;
			}
			rtw_write_rf(rtwdev, path, RF_CFGCH, RFREG_MASK, rf18);
			rtw_write32_mask(rtwdev,
					 three_wire[path], BIT_3WIRE_EN, 0x3);
		}
	}
	rtw8822c_txgapk_write_gain_bb_table(rtwdev);
	txgapk->read_txgain = 1;
}

static void rtw8822c_txgapk(struct rtw_dev *rtwdev)
{
	static const u32 bb_reg[2] = {REG_TX_PTCL_CTRL, REG_TX_FIFO};
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u32 bb_reg_backup[2];
	u8 path;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] ======>%s\n", __func__);

	rtw8822c_txgapk_save_all_tx_gain_table(rtwdev);

	if (txgapk->read_txgain == 0) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] txgapk->read_txgain == 0 return!!!\n");
		return;
	}

	if (rtwdev->efuse.power_track_type >= 4 &&
	    rtwdev->efuse.power_track_type <= 7) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[TXGAPK] Normal Mode in TSSI mode. return!!!\n");
		return;
	}

	rtw8822c_txgapk_backup_bb_reg(rtwdev, bb_reg,
				      bb_reg_backup, ARRAY_SIZE(bb_reg));
	rtw8822c_txgapk_tx_pause(rtwdev);
	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		txgapk->channel = rtw_read_rf(rtwdev, path,
					      RF_CFGCH, RFREG_MASK) & MASKBYTE0;
		rtw8822c_txgapk_bb_dpk(rtwdev, path);
		rtw8822c_txgapk_afe_dpk(rtwdev, path);
		rtw8822c_txgapk_calculate_offset(rtwdev, path);
		rtw8822c_txgapk_rf_restore(rtwdev, path);
		rtw8822c_txgapk_afe_dpk_restore(rtwdev, path);
		rtw8822c_txgapk_bb_dpk_restore(rtwdev, path);
	}
	rtw8822c_txgapk_write_tx_gain(rtwdev);
	rtw8822c_txgapk_reload_bb_reg(rtwdev, bb_reg,
				      bb_reg_backup, ARRAY_SIZE(bb_reg));
}

static void rtw8822c_do_gapk(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm = &rtwdev->dm_info;

	if (dm->dm_flags & BIT(RTW_DM_CAP_TXGAPK)) {
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[TXGAPK] feature disable!!!\n");
		return;
	}
	rtw8822c_rfk_handshake(rtwdev, true);
	rtw8822c_txgapk(rtwdev);
	rtw8822c_rfk_handshake(rtwdev, false);
}

static void rtw8822c_rf_init(struct rtw_dev *rtwdev)
{
	rtw8822c_rf_dac_cal(rtwdev);
	rtw8822c_rf_x2_check(rtwdev);
	rtw8822c_thermal_trim(rtwdev);
	rtw8822c_power_trim(rtwdev);
	rtw8822c_pa_bias(rtwdev);
}

static void rtw8822c_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 path;

	for (path = RF_PATH_A; path < RTW_RF_PATH_MAX; path++) {
		dm_info->delta_power_index[path] = 0;
		ewma_thermal_init(&dm_info->avg_thermal[path]);
		dm_info->thermal_avg[path] = 0xff;
	}

	dm_info->pwr_trk_triggered = false;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
	dm_info->thermal_meter_lck = rtwdev->efuse.thermal_meter_k;
}

static void rtw8822c_phy_set_param(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 crystal_cap;
	u8 cck_gi_u_bnd_msb = 0;
	u8 cck_gi_u_bnd_lsb = 0;
	u8 cck_gi_l_bnd_msb = 0;
	u8 cck_gi_l_bnd_lsb = 0;
	bool is_tx2_path;

	/* power on BB/RF domain */
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN,
		       BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write32_set(rtwdev, REG_WLRF1, BIT_WLRF1_BBRF_EN);

	/* disable low rate DPD */
	rtw_write32_mask(rtwdev, REG_DIS_DPD, DIS_DPD_MASK, DIS_DPD_RATEALL);

	/* pre init before header files config */
	rtw8822c_header_file_init(rtwdev, true);

	rtw_phy_load_tables(rtwdev);

	crystal_cap = rtwdev->efuse.crystal_cap & 0x7f;
	rtw_write32_mask(rtwdev, REG_ANAPAR_XTAL_0, 0xfffc00,
			 crystal_cap | (crystal_cap << 7));

	/* post init after header files config */
	rtw8822c_header_file_init(rtwdev, false);

	is_tx2_path = false;
	rtw8822c_config_trx_mode(rtwdev, hal->antenna_tx, hal->antenna_rx,
				 is_tx2_path);
	rtw_phy_init(rtwdev);

	cck_gi_u_bnd_msb = (u8)rtw_read32_mask(rtwdev, 0x1a98, 0xc000);
	cck_gi_u_bnd_lsb = (u8)rtw_read32_mask(rtwdev, 0x1aa8, 0xf0000);
	cck_gi_l_bnd_msb = (u8)rtw_read32_mask(rtwdev, 0x1a98, 0xc0);
	cck_gi_l_bnd_lsb = (u8)rtw_read32_mask(rtwdev, 0x1a70, 0x0f000000);

	dm_info->cck_gi_u_bnd = ((cck_gi_u_bnd_msb << 4) | (cck_gi_u_bnd_lsb));
	dm_info->cck_gi_l_bnd = ((cck_gi_l_bnd_msb << 4) | (cck_gi_l_bnd_lsb));

	rtw8822c_rf_init(rtwdev);
	rtw8822c_pwrtrack_init(rtwdev);

	rtw_bf_phy_init(rtwdev);
}

#define WLAN_TXQ_RPT_EN		0x1F
#define WLAN_SLOT_TIME		0x09
#define WLAN_PIFS_TIME		0x1C
#define WLAN_SIFS_CCK_CONT_TX	0x0A
#define WLAN_SIFS_OFDM_CONT_TX	0x0E
#define WLAN_SIFS_CCK_TRX	0x0A
#define WLAN_SIFS_OFDM_TRX	0x10
#define WLAN_NAV_MAX		0xC8
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_TBTT_PROHIBIT	0x04 /* unit : 32us */
#define WLAN_TBTT_HOLD_TIME	0x064 /* unit : 32us */
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_BCN_CTRL_CLT0	0x10
#define WLAN_BCN_DMA_TIME	0x02
#define WLAN_BCN_MAX_ERR	0xFF
#define WLAN_SIFS_CCK_DUR_TUNE	0x0A
#define WLAN_SIFS_OFDM_DUR_TUNE	0x10
#define WLAN_SIFS_CCK_CTX	0x0A
#define WLAN_SIFS_CCK_IRX	0x0A
#define WLAN_SIFS_OFDM_CTX	0x0E
#define WLAN_SIFS_OFDM_IRX	0x0E
#define WLAN_EIFS_DUR_TUNE	0x40
#define WLAN_EDCA_VO_PARAM	0x002FA226
#define WLAN_EDCA_VI_PARAM	0x005EA328
#define WLAN_EDCA_BE_PARAM	0x005EA42B
#define WLAN_EDCA_BK_PARAM	0x0000A44F

#define WLAN_RX_FILTER0		0xFFFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)

#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_MAX_AGG_PKT_LIMIT		0x3f
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x3f
#define WLAN_PRE_TXCNT_TIME_TH		0x1E0
#define FAST_EDCA_VO_TH		0x06
#define FAST_EDCA_VI_TH		0x06
#define FAST_EDCA_BE_TH		0x06
#define FAST_EDCA_BK_TH		0x06
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_BAR_ACK_TYPE		0x05
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08
#define WLAN_RESP_TXRATE		0x84
#define WLAN_ACK_TO			0x21
#define WLAN_ACK_TO_CCK			0x6A
#define WLAN_DATA_RATE_FB_CNT_1_4	0x01000000
#define WLAN_DATA_RATE_FB_CNT_5_8	0x08070504
#define WLAN_RTS_RATE_FB_CNT_5_8	0x08070504
#define WLAN_DATA_RATE_FB_RATE0		0xFE01F010
#define WLAN_DATA_RATE_FB_RATE0_H	0x40000000
#define WLAN_RTS_RATE_FB_RATE1		0x003FF010
#define WLAN_RTS_RATE_FB_RATE1_H	0x40000000
#define WLAN_RTS_RATE_FB_RATE4		0x0600F010
#define WLAN_RTS_RATE_FB_RATE4_H	0x400003E0
#define WLAN_RTS_RATE_FB_RATE5		0x0600F015
#define WLAN_RTS_RATE_FB_RATE5_H	0x000000E0
#define WLAN_MULTI_ADDR			0xFFFFFFFF

#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAC_OPT_LB_FUNC1		0x80
#define WLAN_MAC_OPT_FUNC2		0xb0810041
#define WLAN_MAC_INT_MIG_CFG		0x33330000

#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))

#define WLAN_SIFS_DUR_TUNE	(WLAN_SIFS_CCK_DUR_TUNE | \
				(WLAN_SIFS_OFDM_DUR_TUNE << 8))

#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))

#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)

#define MAC_CLK_SPEED	80 /* 80M */
#define EFUSE_PCB_INFO_OFFSET	0xCA

static int rtw8822c_mac_init(struct rtw_dev *rtwdev)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u16 pre_txcnt;

	/* txq control */
	value8 = rtw_read8(rtwdev, REG_FWHW_TXQ_CTRL);
	value8 |= (BIT(7) & ~BIT(1) & ~BIT(2));
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL, value8);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	/* sifs control */
	rtw_write16(rtwdev, REG_SPEC_SIFS, WLAN_SIFS_DUR_TUNE);
	rtw_write32(rtwdev, REG_SIFS, WLAN_SIFS_CFG);
	rtw_write16(rtwdev, REG_RESP_SIFS_CCK,
		    WLAN_SIFS_CCK_CTX | WLAN_SIFS_CCK_IRX << 8);
	rtw_write16(rtwdev, REG_RESP_SIFS_OFDM,
		    WLAN_SIFS_OFDM_CTX | WLAN_SIFS_OFDM_IRX << 8);
	/* rate fallback control */
	rtw_write32(rtwdev, REG_DARFRC, WLAN_DATA_RATE_FB_CNT_1_4);
	rtw_write32(rtwdev, REG_DARFRCH, WLAN_DATA_RATE_FB_CNT_5_8);
	rtw_write32(rtwdev, REG_RARFRCH, WLAN_RTS_RATE_FB_CNT_5_8);
	rtw_write32(rtwdev, REG_ARFR0, WLAN_DATA_RATE_FB_RATE0);
	rtw_write32(rtwdev, REG_ARFRH0, WLAN_DATA_RATE_FB_RATE0_H);
	rtw_write32(rtwdev, REG_ARFR1_V1, WLAN_RTS_RATE_FB_RATE1);
	rtw_write32(rtwdev, REG_ARFRH1_V1, WLAN_RTS_RATE_FB_RATE1_H);
	rtw_write32(rtwdev, REG_ARFR4, WLAN_RTS_RATE_FB_RATE4);
	rtw_write32(rtwdev, REG_ARFRH4, WLAN_RTS_RATE_FB_RATE4_H);
	rtw_write32(rtwdev, REG_ARFR5, WLAN_RTS_RATE_FB_RATE5);
	rtw_write32(rtwdev, REG_ARFRH5, WLAN_RTS_RATE_FB_RATE5_H);
	/* protocol configuration */
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	rtw_write8_set(rtwdev, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);
	pre_txcnt = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
	rtw_write8(rtwdev, REG_PRECNT_CTRL, (u8)(pre_txcnt & 0xFF));
	rtw_write8(rtwdev, REG_PRECNT_CTRL + 1, (u8)(pre_txcnt >> 8));
	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
		  (WLAN_MAX_AGG_PKT_LIMIT << 16) |
		  (WLAN_RTS_MAX_AGG_PKT_LIMIT << 24);
	rtw_write32(rtwdev, REG_PROT_MODE_CTRL, value32);
	rtw_write16(rtwdev, REG_BAR_MODE_CTRL + 2,
		    WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
	/* close BA parser */
	rtw_write8_clr(rtwdev, REG_LIFETIME_EN, BIT_BA_PARSER_EN);
	rtw_write32_clr(rtwdev, REG_RRSR, BITS_RRSR_RSC);

	/* EDCA configuration */
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, WLAN_EDCA_VO_PARAM);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, WLAN_EDCA_VI_PARAM);
	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, WLAN_EDCA_BE_PARAM);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, WLAN_EDCA_BK_PARAM);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_TIME);
	rtw_write8_clr(rtwdev, REG_TX_PTCL_CTRL + 1, BIT_SIFS_BK_EN >> 8);
	rtw_write8_set(rtwdev, REG_RD_CTRL + 1,
		       (BIT_DIS_TXOP_CFE | BIT_DIS_LSIG_CFE |
			BIT_DIS_STBC_CFE) >> 8);

	/* MAC clock configuration */
	rtw_write32_clr(rtwdev, REG_AFE_CTRL1, BIT_MAC_CLK_SEL);
	rtw_write8(rtwdev, REG_USTIME_TSF, MAC_CLK_SPEED);
	rtw_write8(rtwdev, REG_USTIME_EDCA, MAC_CLK_SPEED);

	rtw_write8_set(rtwdev, REG_MISC_CTRL,
		       BIT_EN_FREE_CNT | BIT_DIS_SECOND_CCA);
	rtw_write8_clr(rtwdev, REG_TIMER0_SRC_SEL, BIT_TSFT_SEL_TIMER0);
	rtw_write16(rtwdev, REG_TXPAUSE, 0x0000);
	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write32(rtwdev, REG_RD_NAV_NXT, WLAN_NAV_CFG);
	rtw_write16(rtwdev, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);
	/* Set beacon cotnrol - enable TSF and other related functions */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);
	/* Set send beacon related registers */
	rtw_write32(rtwdev, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	rtw_write8(rtwdev, REG_BCN_CTRL_CLINT0, WLAN_BCN_CTRL_CLT0);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write8(rtwdev, REG_BCN_MAX_ERR, WLAN_BCN_MAX_ERR);

	/* WMAC configuration */
	rtw_write32(rtwdev, REG_MAR, WLAN_MULTI_ADDR);
	rtw_write32(rtwdev, REG_MAR + 4, WLAN_MULTI_ADDR);
	rtw_write8(rtwdev, REG_BBPSF_CTRL + 2, WLAN_RESP_TXRATE);
	rtw_write8(rtwdev, REG_ACKTO, WLAN_ACK_TO);
	rtw_write8(rtwdev, REG_ACKTO_CCK, WLAN_ACK_TO_CCK);
	rtw_write16(rtwdev, REG_EIFS, WLAN_EIFS_DUR_TUNE);
	rtw_write8(rtwdev, REG_NAV_CTRL + 2, WLAN_NAV_MAX);
	rtw_write8(rtwdev, REG_WMAC_TRXPTCL_CTL_H  + 2, WLAN_BAR_ACK_TYPE);
	rtw_write32(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
	rtw_write8(rtwdev, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	rtw_write8(rtwdev, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
	rtw_write32_set(rtwdev, REG_GENERAL_OPTION, BIT_DUMMY_FCS_READY_MASK_EN);
	rtw_write32(rtwdev, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
	rtw_write8(rtwdev, REG_WMAC_OPTION_FUNCTION_1, WLAN_MAC_OPT_NORM_FUNC1);

	/* init low power */
	value16 = rtw_read16(rtwdev, REG_RXPSF_CTRL + 2) & 0xF00F;
	value16 |= (BIT_RXGCK_VHT_FIFOTHR(1) | BIT_RXGCK_HT_FIFOTHR(1) |
		    BIT_RXGCK_OFDM_FIFOTHR(1) | BIT_RXGCK_CCK_FIFOTHR(1)) >> 16;
	rtw_write16(rtwdev, REG_RXPSF_CTRL + 2, value16);
	value16 = 0;
	value16 = BIT_SET_RXPSF_PKTLENTHR(value16, 1);
	value16 |= BIT_RXPSF_CTRLEN | BIT_RXPSF_VHTCHKEN | BIT_RXPSF_HTCHKEN
		| BIT_RXPSF_OFDMCHKEN | BIT_RXPSF_CCKCHKEN
		| BIT_RXPSF_OFDMRST;
	rtw_write16(rtwdev, REG_RXPSF_CTRL, value16);
	rtw_write32(rtwdev, REG_RXPSF_TYPE_CTRL, 0xFFFFFFFF);
	/* rx ignore configuration */
	value16 = rtw_read16(rtwdev, REG_RXPSF_CTRL);
	value16 &= ~(BIT_RXPSF_MHCHKEN | BIT_RXPSF_CCKRST |
		     BIT_RXPSF_CONT_ERRCHKEN);
	value16 = BIT_SET_RXPSF_ERRTHR(value16, 0x07);
	rtw_write16(rtwdev, REG_RXPSF_CTRL, value16);
	rtw_write8_set(rtwdev, REG_SND_PTCL_CTRL,
		       BIT_DIS_CHK_VHTSIGB_CRC);

	/* Interrupt migration configuration */
	rtw_write32(rtwdev, REG_INT_MIG, WLAN_MAC_INT_MIG_CFG);

	return 0;
}

#define FWCD_SIZE_REG_8822C 0x2000
#define FWCD_SIZE_DMEM_8822C 0x10000
#define FWCD_SIZE_IMEM_8822C 0x10000
#define FWCD_SIZE_EMEM_8822C 0x20000
#define FWCD_SIZE_ROM_8822C 0x10000

static const u32 __fwcd_segs_8822c[] = {
	FWCD_SIZE_REG_8822C,
	FWCD_SIZE_DMEM_8822C,
	FWCD_SIZE_IMEM_8822C,
	FWCD_SIZE_EMEM_8822C,
	FWCD_SIZE_ROM_8822C,
};

static const struct rtw_fwcd_segs rtw8822c_fwcd_segs = {
	.segs = __fwcd_segs_8822c,
	.num = ARRAY_SIZE(__fwcd_segs_8822c),
};

static int rtw8822c_dump_fw_crash(struct rtw_dev *rtwdev)
{
#define __dump_fw_8822c(_dev, _mem) \
	rtw_dump_fw(_dev, OCPBASE_ ## _mem ## _88XX, \
		    FWCD_SIZE_ ## _mem ## _8822C, RTW_FWCD_ ## _mem)
	int ret;

	ret = rtw_dump_reg(rtwdev, 0x0, FWCD_SIZE_REG_8822C);
	if (ret)
		return ret;
	ret = __dump_fw_8822c(rtwdev, DMEM);
	if (ret)
		return ret;
	ret = __dump_fw_8822c(rtwdev, IMEM);
	if (ret)
		return ret;
	ret = __dump_fw_8822c(rtwdev, EMEM);
	if (ret)
		return ret;
	ret = __dump_fw_8822c(rtwdev, ROM);
	if (ret)
		return ret;

	return 0;

#undef __dump_fw_8822c
}

static void rtw8822c_rstb_3wire(struct rtw_dev *rtwdev, bool enable)
{
	if (enable) {
		rtw_write32_mask(rtwdev, REG_RSTB, BIT_RSTB_3WIRE, 0x1);
		rtw_write32_mask(rtwdev, REG_ANAPAR_A, BIT_ANAPAR_UPDATE, 0x1);
		rtw_write32_mask(rtwdev, REG_ANAPAR_B, BIT_ANAPAR_UPDATE, 0x1);
	} else {
		rtw_write32_mask(rtwdev, REG_RSTB, BIT_RSTB_3WIRE, 0x0);
	}
}

static void rtw8822c_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
#define RF18_BAND_MASK		(BIT(16) | BIT(9) | BIT(8))
#define RF18_BAND_2G		(0)
#define RF18_BAND_5G		(BIT(16) | BIT(8))
#define RF18_CHANNEL_MASK	(MASKBYTE0)
#define RF18_RFSI_MASK		(BIT(18) | BIT(17))
#define RF18_RFSI_GE_CH80	(BIT(17))
#define RF18_RFSI_GT_CH140	(BIT(18))
#define RF18_BW_MASK		(BIT(13) | BIT(12))
#define RF18_BW_20M		(BIT(13) | BIT(12))
#define RF18_BW_40M		(BIT(13))
#define RF18_BW_80M		(BIT(12))

	u32 rf_reg18 = 0;
	u32 rf_rxbb = 0;

	rf_reg18 = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	rf_reg18 &= ~(RF18_BAND_MASK | RF18_CHANNEL_MASK | RF18_RFSI_MASK |
		      RF18_BW_MASK);

	rf_reg18 |= (IS_CH_2G_BAND(channel) ? RF18_BAND_2G : RF18_BAND_5G);
	rf_reg18 |= (channel & RF18_CHANNEL_MASK);
	if (IS_CH_5G_BAND_4(channel))
		rf_reg18 |= RF18_RFSI_GT_CH140;
	else if (IS_CH_5G_BAND_3(channel))
		rf_reg18 |= RF18_RFSI_GE_CH80;

	switch (bw) {
	case RTW_CHANNEL_WIDTH_5:
	case RTW_CHANNEL_WIDTH_10:
	case RTW_CHANNEL_WIDTH_20:
	default:
		rf_reg18 |= RF18_BW_20M;
		rf_rxbb = 0x18;
		break;
	case RTW_CHANNEL_WIDTH_40:
		/* RF bandwidth */
		rf_reg18 |= RF18_BW_40M;
		rf_rxbb = 0x10;
		break;
	case RTW_CHANNEL_WIDTH_80:
		rf_reg18 |= RF18_BW_80M;
		rf_rxbb = 0x8;
		break;
	}

	rtw8822c_rstb_3wire(rtwdev, false);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, 0x04, 0x01);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, 0x1f, 0x12);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, 0xfffff, rf_rxbb);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, 0x04, 0x00);

	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE2, 0x04, 0x01);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWA, 0x1f, 0x12);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWD0, 0xfffff, rf_rxbb);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE2, 0x04, 0x00);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_reg18);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_reg18);

	rtw8822c_rstb_3wire(rtwdev, true);
}

static void rtw8822c_toggle_igi(struct rtw_dev *rtwdev)
{
	u32 igi;

	igi = rtw_read32_mask(rtwdev, REG_RXIGI, 0x7f);
	rtw_write32_mask(rtwdev, REG_RXIGI, 0x7f, igi - 2);
	rtw_write32_mask(rtwdev, REG_RXIGI, 0x7f00, igi - 2);
	rtw_write32_mask(rtwdev, REG_RXIGI, 0x7f, igi);
	rtw_write32_mask(rtwdev, REG_RXIGI, 0x7f00, igi);
}

static void rtw8822c_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	if (IS_CH_2G_BAND(channel)) {
		rtw_write32_clr(rtwdev, REG_BGCTRL, BITS_RX_IQ_WEIGHT);
		rtw_write32_set(rtwdev, REG_TXF4, BIT(20));
		rtw_write32_clr(rtwdev, REG_CCK_CHECK, BIT_CHECK_CCK_EN);
		rtw_write32_clr(rtwdev, REG_CCKTXONLY, BIT_BB_CCK_CHECK_EN);
		rtw_write32_mask(rtwdev, REG_CCAMSK, 0x3F000000, 0xF);

		switch (bw) {
		case RTW_CHANNEL_WIDTH_20:
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_CCK,
					 0x5);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_CCK,
					 0x5);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_OFDM,
					 0x6);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_OFDM,
					 0x6);
			break;
		case RTW_CHANNEL_WIDTH_40:
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_CCK,
					 0x4);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_CCK,
					 0x4);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_OFDM,
					 0x0);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_OFDM,
					 0x0);
			break;
		}
		if (channel == 13 || channel == 14)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x969);
		else if (channel == 11 || channel == 12)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x96a);
		else
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x9aa);
		if (channel == 14) {
			rtw_write32_mask(rtwdev, REG_TXF0, MASKHWORD, 0x3da0);
			rtw_write32_mask(rtwdev, REG_TXF1, MASKDWORD,
					 0x4962c931);
			rtw_write32_mask(rtwdev, REG_TXF2, MASKLWORD, 0x6aa3);
			rtw_write32_mask(rtwdev, REG_TXF3, MASKHWORD, 0xaa7b);
			rtw_write32_mask(rtwdev, REG_TXF4, MASKLWORD, 0xf3d7);
			rtw_write32_mask(rtwdev, REG_TXF5, MASKDWORD, 0x0);
			rtw_write32_mask(rtwdev, REG_TXF6, MASKDWORD,
					 0xff012455);
			rtw_write32_mask(rtwdev, REG_TXF7, MASKDWORD, 0xffff);
		} else {
			rtw_write32_mask(rtwdev, REG_TXF0, MASKHWORD, 0x5284);
			rtw_write32_mask(rtwdev, REG_TXF1, MASKDWORD,
					 0x3e18fec8);
			rtw_write32_mask(rtwdev, REG_TXF2, MASKLWORD, 0x0a88);
			rtw_write32_mask(rtwdev, REG_TXF3, MASKHWORD, 0xacc4);
			rtw_write32_mask(rtwdev, REG_TXF4, MASKLWORD, 0xc8b2);
			rtw_write32_mask(rtwdev, REG_TXF5, MASKDWORD,
					 0x00faf0de);
			rtw_write32_mask(rtwdev, REG_TXF6, MASKDWORD,
					 0x00122344);
			rtw_write32_mask(rtwdev, REG_TXF7, MASKDWORD,
					 0x0fffffff);
		}
		if (channel == 13)
			rtw_write32_mask(rtwdev, REG_TXDFIR0, 0x70, 0x3);
		else
			rtw_write32_mask(rtwdev, REG_TXDFIR0, 0x70, 0x1);
	} else if (IS_CH_5G_BAND(channel)) {
		rtw_write32_set(rtwdev, REG_CCKTXONLY, BIT_BB_CCK_CHECK_EN);
		rtw_write32_set(rtwdev, REG_CCK_CHECK, BIT_CHECK_CCK_EN);
		rtw_write32_set(rtwdev, REG_BGCTRL, BITS_RX_IQ_WEIGHT);
		rtw_write32_clr(rtwdev, REG_TXF4, BIT(20));
		rtw_write32_mask(rtwdev, REG_CCAMSK, 0x3F000000, 0x22);
		rtw_write32_mask(rtwdev, REG_TXDFIR0, 0x70, 0x3);
		if (IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel)) {
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_OFDM,
					 0x1);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_OFDM,
					 0x1);
		} else if (IS_CH_5G_BAND_3(channel)) {
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_OFDM,
					 0x2);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_OFDM,
					 0x2);
		} else if (IS_CH_5G_BAND_4(channel)) {
			rtw_write32_mask(rtwdev, REG_RXAGCCTL0, BITS_RXAGC_OFDM,
					 0x3);
			rtw_write32_mask(rtwdev, REG_RXAGCCTL, BITS_RXAGC_OFDM,
					 0x3);
		}

		if (channel >= 36 && channel <= 51)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x494);
		else if (channel >= 52 && channel <= 55)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x493);
		else if (channel >= 56 && channel <= 111)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x453);
		else if (channel >= 112 && channel <= 119)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x452);
		else if (channel >= 120 && channel <= 172)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x412);
		else if (channel >= 173 && channel <= 177)
			rtw_write32_mask(rtwdev, REG_SCOTRK, 0xfff, 0x411);
	}

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_DFIRBW, 0x3FF0, 0x19B);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xf, 0x0);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xffc0, 0x0);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700, 0x7);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700000, 0x6);
		rtw_write32_mask(rtwdev, REG_CCK_SOURCE, BIT_NBI_EN, 0x0);
		rtw_write32_mask(rtwdev, REG_SBD, BITS_SUBTUNE, 0x1);
		rtw_write32_mask(rtwdev, REG_PT_CHSMO, BIT_PT_OPT, 0x0);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_CCKSB, BIT(4),
				 (primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0));
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xf, 0x5);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xc0, 0x0);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xff00,
				 (primary_ch_idx | (primary_ch_idx << 4)));
		rtw_write32_mask(rtwdev, REG_CCK_SOURCE, BIT_NBI_EN, 0x1);
		rtw_write32_mask(rtwdev, REG_SBD, BITS_SUBTUNE, 0x1);
		rtw_write32_mask(rtwdev, REG_PT_CHSMO, BIT_PT_OPT, 0x1);
		break;
	case RTW_CHANNEL_WIDTH_80:
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xf, 0xa);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xc0, 0x0);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xff00,
				 (primary_ch_idx | (primary_ch_idx << 4)));
		rtw_write32_mask(rtwdev, REG_SBD, BITS_SUBTUNE, 0x6);
		rtw_write32_mask(rtwdev, REG_PT_CHSMO, BIT_PT_OPT, 0x1);
		break;
	case RTW_CHANNEL_WIDTH_5:
		rtw_write32_mask(rtwdev, REG_DFIRBW, 0x3FF0, 0x2AB);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xf, 0x0);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xffc0, 0x1);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700, 0x4);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700000, 0x4);
		rtw_write32_mask(rtwdev, REG_CCK_SOURCE, BIT_NBI_EN, 0x0);
		rtw_write32_mask(rtwdev, REG_SBD, BITS_SUBTUNE, 0x1);
		rtw_write32_mask(rtwdev, REG_PT_CHSMO, BIT_PT_OPT, 0x0);
		break;
	case RTW_CHANNEL_WIDTH_10:
		rtw_write32_mask(rtwdev, REG_DFIRBW, 0x3FF0, 0x2AB);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xf, 0x0);
		rtw_write32_mask(rtwdev, REG_TXBWCTL, 0xffc0, 0x2);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700, 0x6);
		rtw_write32_mask(rtwdev, REG_TXCLK, 0x700000, 0x5);
		rtw_write32_mask(rtwdev, REG_CCK_SOURCE, BIT_NBI_EN, 0x0);
		rtw_write32_mask(rtwdev, REG_SBD, BITS_SUBTUNE, 0x1);
		rtw_write32_mask(rtwdev, REG_PT_CHSMO, BIT_PT_OPT, 0x0);
		break;
	}
}

static void rtw8822c_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8822c_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8822c_set_channel_rf(rtwdev, channel, bw);
	rtw8822c_toggle_igi(rtwdev);
}

static void rtw8822c_config_cck_rx_path(struct rtw_dev *rtwdev, u8 rx_path)
{
	if (rx_path == BB_PATH_A || rx_path == BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_CCANRX, 0x00060000, 0x0);
		rtw_write32_mask(rtwdev, REG_CCANRX, 0x00600000, 0x0);
	} else if (rx_path == BB_PATH_AB) {
		rtw_write32_mask(rtwdev, REG_CCANRX, 0x00600000, 0x1);
		rtw_write32_mask(rtwdev, REG_CCANRX, 0x00060000, 0x1);
	}

	if (rx_path == BB_PATH_A)
		rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0x0f000000, 0x0);
	else if (rx_path == BB_PATH_B)
		rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0x0f000000, 0x5);
	else if (rx_path == BB_PATH_AB)
		rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0x0f000000, 0x1);
}

static void rtw8822c_config_ofdm_rx_path(struct rtw_dev *rtwdev, u8 rx_path)
{
	if (rx_path == BB_PATH_A || rx_path == BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RXFNCTL, 0x300, 0x0);
		rtw_write32_mask(rtwdev, REG_RXFNCTL, 0x600000, 0x0);
		rtw_write32_mask(rtwdev, REG_AGCSWSH, BIT(17), 0x0);
		rtw_write32_mask(rtwdev, REG_ANTWTPD, BIT(20), 0x0);
		rtw_write32_mask(rtwdev, REG_MRCM, BIT(24), 0x0);
	} else if (rx_path == BB_PATH_AB) {
		rtw_write32_mask(rtwdev, REG_RXFNCTL, 0x300, 0x1);
		rtw_write32_mask(rtwdev, REG_RXFNCTL, 0x600000, 0x1);
		rtw_write32_mask(rtwdev, REG_AGCSWSH, BIT(17), 0x1);
		rtw_write32_mask(rtwdev, REG_ANTWTPD, BIT(20), 0x1);
		rtw_write32_mask(rtwdev, REG_MRCM, BIT(24), 0x1);
	}

	rtw_write32_mask(rtwdev, 0x824, 0x0f000000, rx_path);
	rtw_write32_mask(rtwdev, 0x824, 0x000f0000, rx_path);
}

static void rtw8822c_config_rx_path(struct rtw_dev *rtwdev, u8 rx_path)
{
	rtw8822c_config_cck_rx_path(rtwdev, rx_path);
	rtw8822c_config_ofdm_rx_path(rtwdev, rx_path);
}

static void rtw8822c_config_cck_tx_path(struct rtw_dev *rtwdev, u8 tx_path,
					bool is_tx2_path)
{
	if (tx_path == BB_PATH_A) {
		rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0xf0000000, 0x8);
	} else if (tx_path == BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0xf0000000, 0x4);
	} else {
		if (is_tx2_path)
			rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0xf0000000, 0xc);
		else
			rtw_write32_mask(rtwdev, REG_RXCCKSEL, 0xf0000000, 0x8);
	}
	rtw8822c_bb_reset(rtwdev);
}

static void rtw8822c_config_ofdm_tx_path(struct rtw_dev *rtwdev, u8 tx_path,
					 enum rtw_bb_path tx_path_sel_1ss)
{
	if (tx_path == BB_PATH_A) {
		rtw_write32_mask(rtwdev, REG_ANTMAP0, 0xff, 0x11);
		rtw_write32_mask(rtwdev, REG_TXLGMAP, 0xff, 0x0);
	} else if (tx_path == BB_PATH_B) {
		rtw_write32_mask(rtwdev, REG_ANTMAP0, 0xff, 0x12);
		rtw_write32_mask(rtwdev, REG_TXLGMAP, 0xff, 0x0);
	} else {
		if (tx_path_sel_1ss == BB_PATH_AB) {
			rtw_write32_mask(rtwdev, REG_ANTMAP0, 0xff, 0x33);
			rtw_write32_mask(rtwdev, REG_TXLGMAP, 0xffff, 0x0404);
		} else if (tx_path_sel_1ss == BB_PATH_B) {
			rtw_write32_mask(rtwdev, REG_ANTMAP0, 0xff, 0x32);
			rtw_write32_mask(rtwdev, REG_TXLGMAP, 0xffff, 0x0400);
		} else if (tx_path_sel_1ss == BB_PATH_A) {
			rtw_write32_mask(rtwdev, REG_ANTMAP0, 0xff, 0x31);
			rtw_write32_mask(rtwdev, REG_TXLGMAP, 0xffff, 0x0400);
		}
	}
	rtw8822c_bb_reset(rtwdev);
}

static void rtw8822c_config_tx_path(struct rtw_dev *rtwdev, u8 tx_path,
				    enum rtw_bb_path tx_path_sel_1ss,
				    enum rtw_bb_path tx_path_cck,
				    bool is_tx2_path)
{
	rtw8822c_config_cck_tx_path(rtwdev, tx_path_cck, is_tx2_path);
	rtw8822c_config_ofdm_tx_path(rtwdev, tx_path, tx_path_sel_1ss);
	rtw8822c_bb_reset(rtwdev);
}

static void rtw8822c_config_trx_mode(struct rtw_dev *rtwdev, u8 tx_path,
				     u8 rx_path, bool is_tx2_path)
{
	if ((tx_path | rx_path) & BB_PATH_A)
		rtw_write32_mask(rtwdev, REG_ORITXCODE, MASK20BITS, 0x33312);
	else
		rtw_write32_mask(rtwdev, REG_ORITXCODE, MASK20BITS, 0x11111);
	if ((tx_path | rx_path) & BB_PATH_B)
		rtw_write32_mask(rtwdev, REG_ORITXCODE2, MASK20BITS, 0x33312);
	else
		rtw_write32_mask(rtwdev, REG_ORITXCODE2, MASK20BITS, 0x11111);

	rtw8822c_config_rx_path(rtwdev, rx_path);
	rtw8822c_config_tx_path(rtwdev, tx_path, BB_PATH_A, BB_PATH_A,
				is_tx2_path);

	rtw8822c_toggle_igi(rtwdev);
}

static void query_phy_status_page0(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 l_bnd, u_bnd;
	u8 gain_a, gain_b;
	s8 rx_power[RTW_RF_PATH_MAX];
	s8 min_rx_power = -120;
	u8 rssi;
	u8 channel;
	int path;

	rx_power[RF_PATH_A] = GET_PHY_STAT_P0_PWDB_A(phy_status);
	rx_power[RF_PATH_B] = GET_PHY_STAT_P0_PWDB_B(phy_status);
	l_bnd = dm_info->cck_gi_l_bnd;
	u_bnd = dm_info->cck_gi_u_bnd;
	gain_a = GET_PHY_STAT_P0_GAIN_A(phy_status);
	gain_b = GET_PHY_STAT_P0_GAIN_B(phy_status);
	if (gain_a < l_bnd)
		rx_power[RF_PATH_A] += (l_bnd - gain_a) << 1;
	else if (gain_a > u_bnd)
		rx_power[RF_PATH_A] -= (gain_a - u_bnd) << 1;
	if (gain_b < l_bnd)
		rx_power[RF_PATH_B] += (l_bnd - gain_b) << 1;
	else if (gain_b > u_bnd)
		rx_power[RF_PATH_B] -= (gain_b - u_bnd) << 1;

	rx_power[RF_PATH_A] -= 110;
	rx_power[RF_PATH_B] -= 110;

	channel = GET_PHY_STAT_P0_CHANNEL(phy_status);
	if (channel != 0)
		rtw_set_rx_freq_band(pkt_stat, channel);
	else
		pkt_stat->channel_invalid = true;

	pkt_stat->rx_power[RF_PATH_A] = rx_power[RF_PATH_A];
	pkt_stat->rx_power[RF_PATH_B] = rx_power[RF_PATH_B];

	for (path = 0; path <= rtwdev->hal.rf_path_num; path++) {
		rssi = rtw_phy_rf_power_2_rssi(&pkt_stat->rx_power[path], 1);
		dm_info->rssi[path] = rssi;
	}

	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
}

static void query_phy_status_page1(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_path_div *p_div = &rtwdev->dm_path_div;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 rxsc, bw;
	s8 min_rx_power = -120;
	s8 rx_evm;
	u8 evm_dbm = 0;
	u8 rssi;
	int path;
	u8 channel;

	if (pkt_stat->rate > DESC_RATE11M && pkt_stat->rate < DESC_RATEMCS0)
		rxsc = GET_PHY_STAT_P1_L_RXSC(phy_status);
	else
		rxsc = GET_PHY_STAT_P1_HT_RXSC(phy_status);

	if (rxsc == 0)
		bw = rtwdev->hal.current_band_width;
	else if (rxsc >= 1 && rxsc <= 8)
		bw = RTW_CHANNEL_WIDTH_20;
	else if (rxsc >= 9 && rxsc <= 12)
		bw = RTW_CHANNEL_WIDTH_40;
	else
		bw = RTW_CHANNEL_WIDTH_80;

	channel = GET_PHY_STAT_P1_CHANNEL(phy_status);
	rtw_set_rx_freq_band(pkt_stat, channel);

	pkt_stat->rx_power[RF_PATH_A] = GET_PHY_STAT_P1_PWDB_A(phy_status) - 110;
	pkt_stat->rx_power[RF_PATH_B] = GET_PHY_STAT_P1_PWDB_B(phy_status) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 2);
	pkt_stat->bw = bw;
	pkt_stat->signal_power = max3(pkt_stat->rx_power[RF_PATH_A],
				      pkt_stat->rx_power[RF_PATH_B],
				      min_rx_power);

	dm_info->curr_rx_rate = pkt_stat->rate;

	pkt_stat->rx_evm[RF_PATH_A] = GET_PHY_STAT_P1_RXEVM_A(phy_status);
	pkt_stat->rx_evm[RF_PATH_B] = GET_PHY_STAT_P1_RXEVM_B(phy_status);

	pkt_stat->rx_snr[RF_PATH_A] = GET_PHY_STAT_P1_RXSNR_A(phy_status);
	pkt_stat->rx_snr[RF_PATH_B] = GET_PHY_STAT_P1_RXSNR_B(phy_status);

	pkt_stat->cfo_tail[RF_PATH_A] = GET_PHY_STAT_P1_CFO_TAIL_A(phy_status);
	pkt_stat->cfo_tail[RF_PATH_B] = GET_PHY_STAT_P1_CFO_TAIL_B(phy_status);

	for (path = 0; path <= rtwdev->hal.rf_path_num; path++) {
		rssi = rtw_phy_rf_power_2_rssi(&pkt_stat->rx_power[path], 1);
		dm_info->rssi[path] = rssi;
		if (path == RF_PATH_A) {
			p_div->path_a_sum += rssi;
			p_div->path_a_cnt++;
		} else if (path == RF_PATH_B) {
			p_div->path_b_sum += rssi;
			p_div->path_b_cnt++;
		}
		dm_info->rx_snr[path] = pkt_stat->rx_snr[path] >> 1;
		dm_info->cfo_tail[path] = (pkt_stat->cfo_tail[path] * 5) >> 1;

		rx_evm = pkt_stat->rx_evm[path];

		if (rx_evm < 0) {
			if (rx_evm == S8_MIN)
				evm_dbm = 0;
			else
				evm_dbm = ((u8)-rx_evm >> 1);
		}
		dm_info->rx_evm_dbm[path] = evm_dbm;
	}
	rtw_phy_parsing_cfo(rtwdev, pkt_stat);
}

static void query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			     struct rtw_rx_pkt_stat *pkt_stat)
{
	u8 page;

	page = *phy_status & 0xf;

	switch (page) {
	case 0:
		query_phy_status_page0(rtwdev, phy_status, pkt_stat);
		break;
	case 1:
		query_phy_status_page1(rtwdev, phy_status, pkt_stat);
		break;
	default:
		rtw_warn(rtwdev, "unused phy status page (%d)\n", page);
		return;
	}
}

static void
rtw8822c_set_write_tx_power_ref(struct rtw_dev *rtwdev, u8 *tx_pwr_ref_cck,
				u8 *tx_pwr_ref_ofdm)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 txref_cck[2] = {0x18a0, 0x41a0};
	u32 txref_ofdm[2] = {0x18e8, 0x41e8};
	u8 path;

	for (path = 0; path < hal->rf_path_num; path++) {
		rtw_write32_mask(rtwdev, 0x1c90, BIT(15), 0);
		rtw_write32_mask(rtwdev, txref_cck[path], 0x7f0000,
				 tx_pwr_ref_cck[path]);
	}
	for (path = 0; path < hal->rf_path_num; path++) {
		rtw_write32_mask(rtwdev, 0x1c90, BIT(15), 0);
		rtw_write32_mask(rtwdev, txref_ofdm[path], 0x1fc00,
				 tx_pwr_ref_ofdm[path]);
	}
}

static void rtw8822c_set_tx_power_diff(struct rtw_dev *rtwdev, u8 rate,
				       s8 *diff_idx)
{
	u32 offset_txagc = 0x3a00;
	u8 rate_idx = rate & 0xfc;
	u8 pwr_idx[4];
	u32 phy_pwr_idx;
	int i;

	for (i = 0; i < 4; i++)
		pwr_idx[i] = diff_idx[i] & 0x7f;

	phy_pwr_idx = pwr_idx[0] |
		      (pwr_idx[1] << 8) |
		      (pwr_idx[2] << 16) |
		      (pwr_idx[3] << 24);

	rtw_write32_mask(rtwdev, 0x1c90, BIT(15), 0x0);
	rtw_write32_mask(rtwdev, offset_txagc + rate_idx, MASKDWORD,
			 phy_pwr_idx);
}

static void rtw8822c_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rs, rate, j;
	u8 pwr_ref_cck[2] = {hal->tx_pwr_tbl[RF_PATH_A][DESC_RATE11M],
			     hal->tx_pwr_tbl[RF_PATH_B][DESC_RATE11M]};
	u8 pwr_ref_ofdm[2] = {hal->tx_pwr_tbl[RF_PATH_A][DESC_RATEMCS7],
			      hal->tx_pwr_tbl[RF_PATH_B][DESC_RATEMCS7]};
	s8 diff_a, diff_b;
	u8 pwr_a, pwr_b;
	s8 diff_idx[4];

	rtw8822c_set_write_tx_power_ref(rtwdev, pwr_ref_cck, pwr_ref_ofdm);
	for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++) {
		for (j = 0; j < rtw_rate_size[rs]; j++) {
			rate = rtw_rate_section[rs][j];
			pwr_a = hal->tx_pwr_tbl[RF_PATH_A][rate];
			pwr_b = hal->tx_pwr_tbl[RF_PATH_B][rate];
			if (rs == 0) {
				diff_a = (s8)pwr_a - (s8)pwr_ref_cck[0];
				diff_b = (s8)pwr_b - (s8)pwr_ref_cck[1];
			} else {
				diff_a = (s8)pwr_a - (s8)pwr_ref_ofdm[0];
				diff_b = (s8)pwr_b - (s8)pwr_ref_ofdm[1];
			}
			diff_idx[rate % 4] = min(diff_a, diff_b);
			if (rate % 4 == 3)
				rtw8822c_set_tx_power_diff(rtwdev, rate - 3,
							   diff_idx);
		}
	}
}

static int rtw8822c_set_antenna(struct rtw_dev *rtwdev,
				u32 antenna_tx,
				u32 antenna_rx)
{
	struct rtw_hal *hal = &rtwdev->hal;

	switch (antenna_tx) {
	case BB_PATH_A:
	case BB_PATH_B:
	case BB_PATH_AB:
		break;
	default:
		rtw_warn(rtwdev, "unsupported tx path 0x%x\n", antenna_tx);
		return -EINVAL;
	}

	/* path B only is not available for RX */
	switch (antenna_rx) {
	case BB_PATH_A:
	case BB_PATH_AB:
		break;
	default:
		rtw_warn(rtwdev, "unsupported rx path 0x%x\n", antenna_rx);
		return -EINVAL;
	}

	hal->antenna_tx = antenna_tx;
	hal->antenna_rx = antenna_rx;

	rtw8822c_config_trx_mode(rtwdev, antenna_tx, antenna_rx, false);

	return 0;
}

static void rtw8822c_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_ANAPARLDO_POW_MAC);
	ldo_pwr = enable ? ldo_pwr | BIT_LDOE25_PON : ldo_pwr & ~BIT_LDOE25_PON;
	rtw_write8(rtwdev, REG_ANAPARLDO_POW_MAC, ldo_pwr);
}

static void rtw8822c_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_enable;
	u32 cck_fa_cnt;
	u32 crc32_cnt;
	u32 cca32_cnt;
	u32 ofdm_fa_cnt;
	u32 ofdm_fa_cnt1, ofdm_fa_cnt2, ofdm_fa_cnt3, ofdm_fa_cnt4, ofdm_fa_cnt5;
	u16 parity_fail, rate_illegal, crc8_fail, mcs_fail, sb_search_fail,
	    fast_fsync, crc8_fail_vhta, mcs_fail_vht;

	cck_enable = rtw_read32(rtwdev, REG_ENCCK) & BIT_CCK_BLK_EN;
	cck_fa_cnt = rtw_read16(rtwdev, REG_CCK_FACNT);

	ofdm_fa_cnt1 = rtw_read32(rtwdev, REG_OFDM_FACNT1);
	ofdm_fa_cnt2 = rtw_read32(rtwdev, REG_OFDM_FACNT2);
	ofdm_fa_cnt3 = rtw_read32(rtwdev, REG_OFDM_FACNT3);
	ofdm_fa_cnt4 = rtw_read32(rtwdev, REG_OFDM_FACNT4);
	ofdm_fa_cnt5 = rtw_read32(rtwdev, REG_OFDM_FACNT5);

	parity_fail	= FIELD_GET(GENMASK(31, 16), ofdm_fa_cnt1);
	rate_illegal	= FIELD_GET(GENMASK(15, 0), ofdm_fa_cnt2);
	crc8_fail	= FIELD_GET(GENMASK(31, 16), ofdm_fa_cnt2);
	crc8_fail_vhta	= FIELD_GET(GENMASK(15, 0), ofdm_fa_cnt3);
	mcs_fail	= FIELD_GET(GENMASK(15, 0), ofdm_fa_cnt4);
	mcs_fail_vht	= FIELD_GET(GENMASK(31, 16), ofdm_fa_cnt4);
	fast_fsync	= FIELD_GET(GENMASK(15, 0), ofdm_fa_cnt5);
	sb_search_fail	= FIELD_GET(GENMASK(31, 16), ofdm_fa_cnt5);

	ofdm_fa_cnt = parity_fail + rate_illegal + crc8_fail + crc8_fail_vhta +
		      mcs_fail + mcs_fail_vht + fast_fsync + sb_search_fail;

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt += cck_enable ? cck_fa_cnt : 0;

	crc32_cnt = rtw_read32(rtwdev, 0x2c04);
	dm_info->cck_ok_cnt = crc32_cnt & 0xffff;
	dm_info->cck_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0x2c14);
	dm_info->ofdm_ok_cnt = crc32_cnt & 0xffff;
	dm_info->ofdm_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0x2c10);
	dm_info->ht_ok_cnt = crc32_cnt & 0xffff;
	dm_info->ht_err_cnt = (crc32_cnt & 0xffff0000) >> 16;
	crc32_cnt = rtw_read32(rtwdev, 0x2c0c);
	dm_info->vht_ok_cnt = crc32_cnt & 0xffff;
	dm_info->vht_err_cnt = (crc32_cnt & 0xffff0000) >> 16;

	cca32_cnt = rtw_read32(rtwdev, 0x2c08);
	dm_info->ofdm_cca_cnt = ((cca32_cnt & 0xffff0000) >> 16);
	dm_info->cck_cca_cnt = cca32_cnt & 0xffff;
	dm_info->total_cca_cnt = dm_info->ofdm_cca_cnt;
	if (cck_enable)
		dm_info->total_cca_cnt += dm_info->cck_cca_cnt;

	rtw_write32_mask(rtwdev, REG_CCANRX, BIT_CCK_FA_RST, 0);
	rtw_write32_mask(rtwdev, REG_CCANRX, BIT_CCK_FA_RST, 2);
	rtw_write32_mask(rtwdev, REG_CCANRX, BIT_OFDM_FA_RST, 0);
	rtw_write32_mask(rtwdev, REG_CCANRX, BIT_OFDM_FA_RST, 2);

	/* disable rx clk gating to reset counters */
	rtw_write32_clr(rtwdev, REG_RX_BREAK, BIT_COM_RX_GCK_EN);
	rtw_write32_set(rtwdev, REG_CNT_CTRL, BIT_ALL_CNT_RST);
	rtw_write32_clr(rtwdev, REG_CNT_CTRL, BIT_ALL_CNT_RST);
	rtw_write32_set(rtwdev, REG_RX_BREAK, BIT_COM_RX_GCK_EN);
}

static void rtw8822c_do_lck(struct rtw_dev *rtwdev)
{
	u32 val;

	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_CTRL, RFREG_MASK, 0x80010);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0x1F0FA);
	fsleep(1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_AAC_CTRL, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_AAC, RFREG_MASK, 0x80001);
	read_poll_timeout(rtw_read_rf, val, val != 0x1, 1000, 100000,
			  true, rtwdev, RF_PATH_A, RF_AAC_CTRL, 0x1000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_SYN_PFD, RFREG_MASK, 0x1F0F8);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_SYN_CTRL, RFREG_MASK, 0x80010);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_FAST_LCK, RFREG_MASK, 0x0f000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_FAST_LCK, RFREG_MASK, 0x4f000);
	fsleep(1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_FAST_LCK, RFREG_MASK, 0x0f000);
}

static void rtw8822c_do_iqk(struct rtw_dev *rtwdev)
{
	struct rtw_iqk_para para = {0};
	u8 iqk_chk;
	int ret;

	para.clear = 1;
	rtw_fw_do_iqk(rtwdev, &para);

	ret = read_poll_timeout(rtw_read8, iqk_chk, iqk_chk == IQK_DONE_8822C,
				20000, 300000, false, rtwdev, REG_RPT_CIP);
	if (ret)
		rtw_warn(rtwdev, "failed to poll iqk status bit\n");

	rtw_write8(rtwdev, REG_IQKSTAT, 0x0);
}

/* for coex */
static void rtw8822c_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT nterrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate */
	/* 0x790[5:0]=0x5 */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x1);

	/* enable PTA (3-wire function form BT side) */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);

	/* enable PTA (tx/rx signal form WiFi side) */
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);
	/* wl tx signal to PTA not case EDCCA */
	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT_PTA_EDCCA_EN);
	/* GNT_BT=1 while select both */
	rtw_write16_set(rtwdev, REG_BT_COEX_V2, BIT_GNT_BT_POLARITY);
	/* BT_CCA = ~GNT_WL_BB, not or GNT_BT_BB, LTE_Rx */
	rtw_write8_clr(rtwdev, REG_DUMMY_PAGE4_V1, BIT_BTCCA_CTRL);

	/* to avoid RF parameter error */
	rtw_write_rf(rtwdev, RF_PATH_B, RF_MODOPT, 0xfffff, 0x40000);
}

static void rtw8822c_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_stat *coex_stat = &coex->stat;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u32 rf_0x1;

	if (coex_stat->gnt_workaround_state == coex_stat->wl_coex_mode)
		return;

	coex_stat->gnt_workaround_state = coex_stat->wl_coex_mode;

	if ((coex_stat->kt_ver == 0 && coex->under_5g) || coex->freerun)
		rf_0x1 = 0x40021;
	else
		rf_0x1 = 0x40000;

	/* BT at S1 for Shared-Ant */
	if (efuse->share_ant)
		rf_0x1 |= BIT(13);

	rtw_write_rf(rtwdev, RF_PATH_B, 0x1, 0xfffff, rf_0x1);

	/* WL-S0 2G RF TRX cannot be masked by GNT_BT
	 * enable "WLS0 BB chage RF mode if GNT_BT = 1" for shared-antenna type
	 * disable:0x1860[3] = 1, enable:0x1860[3] = 0
	 *
	 * enable "DAC off if GNT_WL = 0" for non-shared-antenna
	 * disable 0x1c30[22] = 0,
	 * enable: 0x1c30[22] = 1, 0x1c38[12] = 0, 0x1c38[28] = 1
	 */
	if (coex_stat->wl_coex_mode == COEX_WLINK_2GFREE) {
		rtw_write8_mask(rtwdev, REG_ANAPAR + 2,
				BIT_ANAPAR_BTPS >> 16, 0);
	} else {
		rtw_write8_mask(rtwdev, REG_ANAPAR + 2,
				BIT_ANAPAR_BTPS >> 16, 1);
		rtw_write8_mask(rtwdev, REG_RSTB_SEL + 1,
				BIT_DAC_OFF_ENABLE, 0);
		rtw_write8_mask(rtwdev, REG_RSTB_SEL + 3,
				BIT_DAC_OFF_ENABLE, 1);
	}

	/* disable WL-S1 BB chage RF mode if GNT_BT
	 * since RF TRx mask can do it
	 */
	rtw_write8_mask(rtwdev, REG_IGN_GNTBT4,
			BIT_PI_IGNORE_GNT_BT, 1);

	/* disable WL-S0 BB chage RF mode if wifi is at 5G,
	 * or antenna path is separated
	 */
	if (coex_stat->wl_coex_mode == COEX_WLINK_2GFREE) {
		rtw_write8_mask(rtwdev, REG_IGN_GNT_BT1,
				BIT_PI_IGNORE_GNT_BT, 1);
		rtw_write8_mask(rtwdev, REG_NOMASK_TXBT,
				BIT_NOMASK_TXBT_ENABLE, 1);
	} else if (coex_stat->wl_coex_mode == COEX_WLINK_5G ||
	    coex->under_5g || !efuse->share_ant) {
		if (coex_stat->kt_ver >= 3) {
			rtw_write8_mask(rtwdev, REG_IGN_GNT_BT1,
					BIT_PI_IGNORE_GNT_BT, 0);
			rtw_write8_mask(rtwdev, REG_NOMASK_TXBT,
					BIT_NOMASK_TXBT_ENABLE, 1);
		} else {
			rtw_write8_mask(rtwdev, REG_IGN_GNT_BT1,
					BIT_PI_IGNORE_GNT_BT, 1);
		}
	} else {
		/* shared-antenna */
		rtw_write8_mask(rtwdev, REG_IGN_GNT_BT1,
				BIT_PI_IGNORE_GNT_BT, 0);
		if (coex_stat->kt_ver >= 3) {
			rtw_write8_mask(rtwdev, REG_NOMASK_TXBT,
					BIT_NOMASK_TXBT_ENABLE, 0);
		}
	}
}

static void rtw8822c_coex_cfg_gnt_debug(struct rtw_dev *rtwdev)
{
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 2, BIT_BTGP_SPI_EN >> 16, 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 3, BIT_BTGP_JTAG_EN >> 24, 0);
	rtw_write8_mask(rtwdev, REG_GPIO_MUXCFG + 2, BIT_FSPI_EN >> 16, 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 1, BIT_LED1DIS >> 8, 0);
	rtw_write8_mask(rtwdev, REG_SYS_SDIO_CTRL + 3, BIT_DBG_GNT_WL_BT >> 24, 0);
}

static void rtw8822c_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	struct rtw_efuse *efuse = &rtwdev->efuse;

	coex_rfe->rfe_module_type = rtwdev->efuse.rfe_option;
	coex_rfe->ant_switch_polarity = 0;
	coex_rfe->ant_switch_exist = false;
	coex_rfe->ant_switch_with_bt = false;
	coex_rfe->ant_switch_diversity = false;

	if (efuse->share_ant)
		coex_rfe->wlg_at_btg = true;
	else
		coex_rfe->wlg_at_btg = false;

	/* disable LTE coex in wifi side */
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, BIT_LTE_COEX_EN, 0x0);
	rtw_coex_write_indirect_reg(rtwdev, LTE_WL_TRX_CTRL, MASKLWORD, 0xffff);
	rtw_coex_write_indirect_reg(rtwdev, LTE_BT_TRX_CTRL, MASKLWORD, 0xffff);
}

static void rtw8822c_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	if (wl_pwr == coex_dm->cur_wl_pwr_lvl)
		return;

	coex_dm->cur_wl_pwr_lvl = wl_pwr;
}

static void rtw8822c_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;

	if (low_gain == coex_dm->cur_wl_rx_low_gain_en)
		return;

	coex_dm->cur_wl_rx_low_gain_en = low_gain;

	if (coex_dm->cur_wl_rx_low_gain_en) {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], Hi-Li Table On!\n");

		/* set Rx filter corner RCK offset */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCKD, RFREG_MASK, 0x22);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCK, RFREG_MASK, 0x36);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCKD, RFREG_MASK, 0x22);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCK, RFREG_MASK, 0x36);

	} else {
		rtw_dbg(rtwdev, RTW_DBG_COEX, "[BTCoex], Hi-Li Table Off!\n");

		/* set Rx filter corner RCK offset */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCKD, RFREG_MASK, 0x20);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RCK, RFREG_MASK, 0x0);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCKD, RFREG_MASK, 0x20);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RCK, RFREG_MASK, 0x0);
	}
}

static void rtw8822c_bf_enable_bfee_su(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee)
{
	u8 csi_rsc = 0;
	u32 tmp6dc;

	rtw_bf_enable_bfee_su(rtwdev, vif, bfee);

	tmp6dc = rtw_read32(rtwdev, REG_BBPSF_CTRL) |
			    BIT_WMAC_USE_NDPARATE |
			    (csi_rsc << 13);
	if (vif->net_type == RTW_NET_AP_MODE)
		rtw_write32(rtwdev, REG_BBPSF_CTRL, tmp6dc | BIT(12));
	else
		rtw_write32(rtwdev, REG_BBPSF_CTRL, tmp6dc & ~BIT(12));

	rtw_write32(rtwdev, REG_CSI_RRSR, 0x550);
}

static void rtw8822c_bf_config_bfee_su(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw8822c_bf_enable_bfee_su(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_su(rtwdev, bfee);
}

static void rtw8822c_bf_config_bfee_mu(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw_bf_enable_bfee_mu(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_mu(rtwdev, bfee);
}

static void rtw8822c_bf_config_bfee(struct rtw_dev *rtwdev, struct rtw_vif *vif,
				    struct rtw_bfee *bfee, bool enable)
{
	if (bfee->role == RTW_BFEE_SU)
		rtw8822c_bf_config_bfee_su(rtwdev, vif, bfee, enable);
	else if (bfee->role == RTW_BFEE_MU)
		rtw8822c_bf_config_bfee_mu(rtwdev, vif, bfee, enable);
	else
		rtw_warn(rtwdev, "wrong bfee role\n");
}

struct dpk_cfg_pair {
	u32 addr;
	u32 bitmask;
	u32 data;
};

void rtw8822c_parse_tbl_dpk(struct rtw_dev *rtwdev,
			    const struct rtw_table *tbl)
{
	const struct dpk_cfg_pair *p = tbl->data;
	const struct dpk_cfg_pair *end = p + tbl->size / 3;

	BUILD_BUG_ON(sizeof(struct dpk_cfg_pair) != sizeof(u32) * 3);

	for (; p < end; p++)
		rtw_write32_mask(rtwdev, p->addr, p->bitmask, p->data);
}

static void rtw8822c_dpk_set_gnt_wl(struct rtw_dev *rtwdev, bool is_before_k)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	if (is_before_k) {
		dpk_info->gnt_control = rtw_read32(rtwdev, 0x70);
		dpk_info->gnt_value = rtw_coex_read_indirect_reg(rtwdev, 0x38);
		rtw_write32_mask(rtwdev, 0x70, BIT(26), 0x1);
		rtw_coex_write_indirect_reg(rtwdev, 0x38, MASKBYTE1, 0x77);
	} else {
		rtw_coex_write_indirect_reg(rtwdev, 0x38, MASKDWORD,
					    dpk_info->gnt_value);
		rtw_write32(rtwdev, 0x70, dpk_info->gnt_control);
	}
}

static void
rtw8822c_dpk_restore_registers(struct rtw_dev *rtwdev, u32 reg_num,
			       struct rtw_backup_info *bckp)
{
	rtw_restore_reg(rtwdev, bckp, reg_num);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_DPD_CLK, 0x4);
}

static void
rtw8822c_dpk_backup_registers(struct rtw_dev *rtwdev, u32 *reg,
			      u32 reg_num, struct rtw_backup_info *bckp)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		bckp[i].len = 4;
		bckp[i].reg = reg[i];
		bckp[i].val = rtw_read32(rtwdev, reg[i]);
	}
}

static void rtw8822c_dpk_backup_rf_registers(struct rtw_dev *rtwdev,
					     u32 *rf_reg,
					     u32 rf_reg_bak[][2])
{
	u32 i;

	for (i = 0; i < DPK_RF_REG_NUM; i++) {
		rf_reg_bak[i][RF_PATH_A] = rtw_read_rf(rtwdev, RF_PATH_A,
						       rf_reg[i], RFREG_MASK);
		rf_reg_bak[i][RF_PATH_B] = rtw_read_rf(rtwdev, RF_PATH_B,
						       rf_reg[i], RFREG_MASK);
	}
}

static void rtw8822c_dpk_reload_rf_registers(struct rtw_dev *rtwdev,
					     u32 *rf_reg,
					     u32 rf_reg_bak[][2])
{
	u32 i;

	for (i = 0; i < DPK_RF_REG_NUM; i++) {
		rtw_write_rf(rtwdev, RF_PATH_A, rf_reg[i], RFREG_MASK,
			     rf_reg_bak[i][RF_PATH_A]);
		rtw_write_rf(rtwdev, RF_PATH_B, rf_reg[i], RFREG_MASK,
			     rf_reg_bak[i][RF_PATH_B]);
	}
}

static void rtw8822c_dpk_information(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32  reg;
	u8 band_shift;

	reg = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	band_shift = FIELD_GET(BIT(16), reg);
	dpk_info->dpk_band = 1 << band_shift;
	dpk_info->dpk_ch = FIELD_GET(0xff, reg);
	dpk_info->dpk_bw = FIELD_GET(0x3000, reg);
}

static void rtw8822c_dpk_rxbb_dc_cal(struct rtw_dev *rtwdev, u8 path)
{
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84800);
	udelay(5);
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84801);
	usleep_range(600, 610);
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84800);
}

static u8 rtw8822c_dpk_dc_corr_check(struct rtw_dev *rtwdev, u8 path)
{
	u16 dc_i, dc_q;
	u8 corr_idx;

	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000900f0);
	dc_i = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(27, 16));
	dc_q = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(11, 0));

	if (dc_i & BIT(11))
		dc_i = 0x1000 - dc_i;
	if (dc_q & BIT(11))
		dc_q = 0x1000 - dc_q;

	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);
	corr_idx = (u8)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(7, 0));
	rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(15, 8));

	if (dc_i > 200 || dc_q > 200 || corr_idx < 40 || corr_idx > 65)
		return 1;
	else
		return 0;

}

static void rtw8822c_dpk_tx_pause(struct rtw_dev *rtwdev)
{
	u8 reg_a, reg_b;
	u16 count = 0;

	rtw_write8(rtwdev, 0x522, 0xff);
	rtw_write32_mask(rtwdev, 0x1e70, 0xf, 0x2);

	do {
		reg_a = (u8)rtw_read_rf(rtwdev, RF_PATH_A, 0x00, 0xf0000);
		reg_b = (u8)rtw_read_rf(rtwdev, RF_PATH_B, 0x00, 0xf0000);
		udelay(2);
		count++;
	} while ((reg_a == 2 || reg_b == 2) && count < 2500);
}

static void rtw8822c_dpk_mac_bb_setting(struct rtw_dev *rtwdev)
{
	rtw8822c_dpk_tx_pause(rtwdev);
	rtw_load_table(rtwdev, &rtw8822c_dpk_mac_bb_tbl);
}

static void rtw8822c_dpk_afe_setting(struct rtw_dev *rtwdev, bool is_do_dpk)
{
	if (is_do_dpk)
		rtw_load_table(rtwdev, &rtw8822c_dpk_afe_is_dpk_tbl);
	else
		rtw_load_table(rtwdev, &rtw8822c_dpk_afe_no_dpk_tbl);
}

static void rtw8822c_dpk_pre_setting(struct rtw_dev *rtwdev)
{
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write_rf(rtwdev, path, RF_RXAGC_OFFSET, RFREG_MASK, 0x0);
		rtw_write32(rtwdev, REG_NCTL0, 0x8 | (path << 1));
		if (rtwdev->dm_info.dpk_info.dpk_band == RTW_BAND_2G)
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f100000);
		else
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f0d0000);
		rtw_write32_mask(rtwdev, REG_DPD_LUT0, BIT_GLOSS_DB, 0x4);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x3);
	}
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32(rtwdev, REG_DPD_CTL11, 0x3b23170b);
	rtw_write32(rtwdev, REG_DPD_CTL12, 0x775f5347);
}

static u32 rtw8822c_dpk_rf_setting(struct rtw_dev *rtwdev, u8 path)
{
	u32 ori_txbb;

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x50017);
	ori_txbb = rtw_read_rf(rtwdev, path, RF_TX_GAIN, RFREG_MASK);

	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_PWR_TRIM, 0x1);
	rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_BB_GAIN, 0x0);
	rtw_write_rf(rtwdev, path, RF_TX_GAIN, RFREG_MASK, ori_txbb);

	if (rtwdev->dm_info.dpk_info.dpk_band == RTW_BAND_2G) {
		rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_RF_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_RXG_GAIN, BIT_RXG_GAIN, 0x0);
	} else {
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_TXA_LB_ATT, 0x0);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_ATT, 0x6);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_SW, 0x1);
		rtw_write_rf(rtwdev, path, RF_RXA_MIX_GAIN, BIT_RXA_MIX_GAIN, 0);
	}

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0xf);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
	rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);

	if (rtwdev->dm_info.dpk_info.dpk_bw == DPK_CHANNEL_WIDTH_80)
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x2);
	else
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x1);

	rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT(1), 0x1);

	usleep_range(100, 110);

	return ori_txbb & 0x1f;
}

static u16 rtw8822c_dpk_get_cmd(struct rtw_dev *rtwdev, u8 action, u8 path)
{
	u16 cmd;
	u8 bw = rtwdev->dm_info.dpk_info.dpk_bw == DPK_CHANNEL_WIDTH_80 ? 2 : 0;

	switch (action) {
	case RTW_DPK_GAIN_LOSS:
		cmd = 0x14 + path;
		break;
	case RTW_DPK_DO_DPK:
		cmd = 0x16 + path + bw;
		break;
	case RTW_DPK_DPK_ON:
		cmd = 0x1a + path;
		break;
	case RTW_DPK_DAGC:
		cmd = 0x1c + path + bw;
		break;
	default:
		return 0;
	}

	return (cmd << 8) | 0x48;
}

static u8 rtw8822c_dpk_one_shot(struct rtw_dev *rtwdev, u8 path, u8 action)
{
	u16 dpk_cmd;
	u8 result = 0;

	rtw8822c_dpk_set_gnt_wl(rtwdev, true);

	if (action == RTW_DPK_CAL_PWR) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(12), 0x1);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(12), 0x0);
		rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x0);
		msleep(10);
		if (!check_hw_ready(rtwdev, REG_STAT_RPT, BIT(31), 0x1)) {
			result = 1;
			rtw_dbg(rtwdev, RTW_DBG_RFK, "[DPK] one-shot over 20ms\n");
		}
	} else {
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x9);

		dpk_cmd = rtw8822c_dpk_get_cmd(rtwdev, action, path);
		rtw_write32(rtwdev, REG_NCTL0, dpk_cmd);
		rtw_write32(rtwdev, REG_NCTL0, dpk_cmd + 1);
		msleep(10);
		if (!check_hw_ready(rtwdev, 0x2d9c, 0xff, 0x55)) {
			result = 1;
			rtw_dbg(rtwdev, RTW_DBG_RFK, "[DPK] one-shot over 20ms\n");
		}
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x0);
	}

	rtw8822c_dpk_set_gnt_wl(rtwdev, false);

	rtw_write8(rtwdev, 0x1b10, 0x0);

	return result;
}

static u16 rtw8822c_dpk_dgain_read(struct rtw_dev *rtwdev, u8 path)
{
	u16 dgain;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, 0x00ff0000, 0x0);

	dgain = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(27, 16));

	return dgain;
}

static u8 rtw8822c_dpk_thermal_read(struct rtw_dev *rtwdev, u8 path)
{
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x1);
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x0);
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x1);
	udelay(15);

	return (u8)rtw_read_rf(rtwdev, path, RF_T_METER, 0x0007e);
}

static u32 rtw8822c_dpk_pas_read(struct rtw_dev *rtwdev, u8 path)
{
	u32 i_val, q_val;

	rtw_write32(rtwdev, REG_NCTL0, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x0);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x00060001);
	rtw_write32(rtwdev, 0x1b4c, 0x00000000);
	rtw_write32(rtwdev, 0x1b4c, 0x00080000);

	q_val = rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKHWORD);
	i_val = rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKLWORD);

	if (i_val & BIT(15))
		i_val = 0x10000 - i_val;
	if (q_val & BIT(15))
		q_val = 0x10000 - q_val;

	rtw_write32(rtwdev, 0x1b4c, 0x00000000);

	return i_val * i_val + q_val * q_val;
}

static u32 rtw8822c_psd_log2base(u32 val)
{
	u32 tmp, val_integerd_b, tindex;
	u32 result, val_fractiond_b;
	u32 table_fraction[21] = {0, 432, 332, 274, 232, 200, 174,
				  151, 132, 115, 100, 86, 74, 62, 51,
				  42, 32, 23, 15, 7, 0};

	if (val == 0)
		return 0;

	val_integerd_b = __fls(val) + 1;

	tmp = (val * 100) / (1 << val_integerd_b);
	tindex = tmp / 5;

	if (tindex >= ARRAY_SIZE(table_fraction))
		tindex = ARRAY_SIZE(table_fraction) - 1;

	val_fractiond_b = table_fraction[tindex];

	result = val_integerd_b * 100 - val_fractiond_b;

	return result;
}

static u8 rtw8822c_dpk_gainloss_result(struct rtw_dev *rtwdev, u8 path)
{
	u8 result;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x1);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x00060000);

	result = (u8)rtw_read32_mask(rtwdev, REG_STAT_RPT, 0x000000f0);

	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x0);

	return result;
}

static u8 rtw8822c_dpk_agc_gain_chk(struct rtw_dev *rtwdev, u8 path,
				    u8 limited_pga)
{
	u8 result = 0;
	u16 dgain;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
	dgain = rtw8822c_dpk_dgain_read(rtwdev, path);

	if (dgain > 1535 && !limited_pga)
		return RTW_DPK_GAIN_LESS;
	else if (dgain < 768 && !limited_pga)
		return RTW_DPK_GAIN_LARGE;
	else
		return result;
}

static u8 rtw8822c_dpk_agc_loss_chk(struct rtw_dev *rtwdev, u8 path)
{
	u32 loss, loss_db;

	loss = rtw8822c_dpk_pas_read(rtwdev, path);
	if (loss < 0x4000000)
		return RTW_DPK_GL_LESS;
	loss_db = 3 * rtw8822c_psd_log2base(loss >> 13) - 3870;

	if (loss_db > 1000)
		return RTW_DPK_GL_LARGE;
	else if (loss_db < 250)
		return RTW_DPK_GL_LESS;
	else
		return RTW_DPK_AGC_OUT;
}

struct rtw8822c_dpk_data {
	u8 txbb;
	u8 pga;
	u8 limited_pga;
	u8 agc_cnt;
	bool loss_only;
	bool gain_only;
	u8 path;
};

static u8 rtw8822c_gain_check_state(struct rtw_dev *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 state;

	data->txbb = (u8)rtw_read_rf(rtwdev, data->path, RF_TX_GAIN,
				     BIT_GAIN_TXBB);
	data->pga = (u8)rtw_read_rf(rtwdev, data->path, RF_MODE_TRXAGC,
				    BIT_RXAGC);

	if (data->loss_only) {
		state = RTW_DPK_LOSS_CHECK;
		goto check_end;
	}

	state = rtw8822c_dpk_agc_gain_chk(rtwdev, data->path,
					  data->limited_pga);
	if (state == RTW_DPK_GAIN_CHECK && data->gain_only)
		state = RTW_DPK_AGC_OUT;
	else if (state == RTW_DPK_GAIN_CHECK)
		state = RTW_DPK_LOSS_CHECK;

check_end:
	data->agc_cnt++;
	if (data->agc_cnt >= 6)
		state = RTW_DPK_AGC_OUT;

	return state;
}

static u8 rtw8822c_gain_large_state(struct rtw_dev *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 pga = data->pga;

	if (pga > 0xe)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xc);
	else if (pga > 0xb && pga < 0xf)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0x0);
	else if (pga < 0xc)
		data->limited_pga = 1;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gain_less_state(struct rtw_dev *rtwdev,
				   struct rtw8822c_dpk_data *data)
{
	u8 pga = data->pga;

	if (pga < 0xc)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xc);
	else if (pga > 0xb && pga < 0xf)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xf);
	else if (pga > 0xe)
		data->limited_pga = 1;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gl_state(struct rtw_dev *rtwdev,
			    struct rtw8822c_dpk_data *data, u8 is_large)
{
	u8 txbb_bound[] = {0x1f, 0};

	if (data->txbb == txbb_bound[is_large])
		return RTW_DPK_AGC_OUT;

	if (is_large == 1)
		data->txbb -= 2;
	else
		data->txbb += 3;

	rtw_write_rf(rtwdev, data->path, RF_TX_GAIN, BIT_GAIN_TXBB, data->txbb);
	data->limited_pga = 0;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gl_large_state(struct rtw_dev *rtwdev,
				  struct rtw8822c_dpk_data *data)
{
	return rtw8822c_gl_state(rtwdev, data, 1);
}

static u8 rtw8822c_gl_less_state(struct rtw_dev *rtwdev,
				 struct rtw8822c_dpk_data *data)
{
	return rtw8822c_gl_state(rtwdev, data, 0);
}

static u8 rtw8822c_loss_check_state(struct rtw_dev *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 path = data->path;
	u8 state;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_GAIN_LOSS);
	state = rtw8822c_dpk_agc_loss_chk(rtwdev, path);

	return state;
}

static u8 (*dpk_state[])(struct rtw_dev *rtwdev,
			  struct rtw8822c_dpk_data *data) = {
	rtw8822c_gain_check_state, rtw8822c_gain_large_state,
	rtw8822c_gain_less_state, rtw8822c_gl_large_state,
	rtw8822c_gl_less_state, rtw8822c_loss_check_state };

static u8 rtw8822c_dpk_pas_agc(struct rtw_dev *rtwdev, u8 path,
			       bool gain_only, bool loss_only)
{
	struct rtw8822c_dpk_data data = {0};
	u8 (*func)(struct rtw_dev *rtwdev, struct rtw8822c_dpk_data *data);
	u8 state = RTW_DPK_GAIN_CHECK;

	data.loss_only = loss_only;
	data.gain_only = gain_only;
	data.path = path;

	for (;;) {
		func = dpk_state[state];
		state = func(rtwdev, &data);
		if (state == RTW_DPK_AGC_OUT)
			break;
	}

	return data.txbb;
}

static bool rtw8822c_dpk_coef_iq_check(struct rtw_dev *rtwdev,
				       u16 coef_i, u16 coef_q)
{
	if (coef_i == 0x1000 || coef_i == 0x0fff ||
	    coef_q == 0x1000 || coef_q == 0x0fff)
		return true;

	return false;
}

static u32 rtw8822c_dpk_coef_transfer(struct rtw_dev *rtwdev)
{
	u32 reg = 0;
	u16 coef_i = 0, coef_q = 0;

	reg = rtw_read32(rtwdev, REG_STAT_RPT);

	coef_i = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKHWORD) & 0x1fff;
	coef_q = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKLWORD) & 0x1fff;

	coef_q = ((0x2000 - coef_q) & 0x1fff) - 1;

	reg = (coef_i << 16) | coef_q;

	return reg;
}

static const u32 rtw8822c_dpk_get_coef_tbl[] = {
	0x000400f0, 0x040400f0, 0x080400f0, 0x010400f0, 0x050400f0,
	0x090400f0, 0x020400f0, 0x060400f0, 0x0a0400f0, 0x030400f0,
	0x070400f0, 0x0b0400f0, 0x0c0400f0, 0x100400f0, 0x0d0400f0,
	0x110400f0, 0x0e0400f0, 0x120400f0, 0x0f0400f0, 0x130400f0,
};

static void rtw8822c_dpk_coef_tbl_apply(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	int i;

	for (i = 0; i < 20; i++) {
		rtw_write32(rtwdev, REG_RXSRAM_CTL,
			    rtw8822c_dpk_get_coef_tbl[i]);
		dpk_info->coef[path][i] = rtw8822c_dpk_coef_transfer(rtwdev);
	}
}

static void rtw8822c_dpk_get_coef(struct rtw_dev *rtwdev, u8 path)
{
	rtw_write32(rtwdev, REG_NCTL0, 0x0000000c);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(24), 0x0);
		rtw_write32(rtwdev, REG_DPD_CTL0_S0, 0x30000080);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(24), 0x1);
		rtw_write32(rtwdev, REG_DPD_CTL0_S1, 0x30000080);
	}

	rtw8822c_dpk_coef_tbl_apply(rtwdev, path);
}

static u8 rtw8822c_dpk_coef_read(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 addr, result = 1;
	u16 coef_i, coef_q;

	for (addr = 0; addr < 20; addr++) {
		coef_i = FIELD_GET(0x1fff0000, dpk_info->coef[path][addr]);
		coef_q = FIELD_GET(0x1fff, dpk_info->coef[path][addr]);

		if (rtw8822c_dpk_coef_iq_check(rtwdev, coef_i, coef_q)) {
			result = 0;
			break;
		}
	}
	return result;
}

static void rtw8822c_dpk_coef_write(struct rtw_dev *rtwdev, u8 path, u8 result)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u16 reg[DPK_RF_PATH_NUM] = {0x1b0c, 0x1b64};
	u32 coef;
	u8 addr;

	rtw_write32(rtwdev, REG_NCTL0, 0x0000000c);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);

	for (addr = 0; addr < 20; addr++) {
		if (result == 0) {
			if (addr == 3)
				coef = 0x04001fff;
			else
				coef = 0x00001fff;
		} else {
			coef = dpk_info->coef[path][addr];
		}
		rtw_write32(rtwdev, reg[path] + addr * 4, coef);
	}
}

static void rtw8822c_dpk_fill_result(struct rtw_dev *rtwdev, u32 dpk_txagc,
				     u8 path, u8 result)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));

	if (result)
		rtw_write8(rtwdev, REG_DPD_AGC, (u8)(dpk_txagc - 6));
	else
		rtw_write8(rtwdev, REG_DPD_AGC, 0x00);

	dpk_info->result[path] = result;
	dpk_info->dpk_txagc[path] = rtw_read8(rtwdev, REG_DPD_AGC);

	rtw8822c_dpk_coef_write(rtwdev, path, result);
}

static u32 rtw8822c_dpk_gainloss(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 tx_agc, tx_bb, ori_txbb, ori_txagc, tx_agc_search, t1, t2;

	ori_txbb = rtw8822c_dpk_rf_setting(rtwdev, path);
	ori_txagc = (u8)rtw_read_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_TXAGC);

	rtw8822c_dpk_rxbb_dc_cal(rtwdev, path);
	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
	rtw8822c_dpk_dgain_read(rtwdev, path);

	if (rtw8822c_dpk_dc_corr_check(rtwdev, path)) {
		rtw8822c_dpk_rxbb_dc_cal(rtwdev, path);
		rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
		rtw8822c_dpk_dc_corr_check(rtwdev, path);
	}

	t1 = rtw8822c_dpk_thermal_read(rtwdev, path);
	tx_bb = rtw8822c_dpk_pas_agc(rtwdev, path, false, true);
	tx_agc_search = rtw8822c_dpk_gainloss_result(rtwdev, path);

	if (tx_bb < tx_agc_search)
		tx_bb = 0;
	else
		tx_bb = tx_bb - tx_agc_search;

	rtw_write_rf(rtwdev, path, RF_TX_GAIN, BIT_GAIN_TXBB, tx_bb);

	tx_agc = ori_txagc - (ori_txbb - tx_bb);

	t2 = rtw8822c_dpk_thermal_read(rtwdev, path);

	dpk_info->thermal_dpk_delta[path] = abs(t2 - t1);

	return tx_agc;
}

static u8 rtw8822c_dpk_by_path(struct rtw_dev *rtwdev, u32 tx_agc, u8 path)
{
	u8 result;

	result = rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DO_DPK);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));

	result = result | (u8)rtw_read32_mask(rtwdev, REG_DPD_CTL1_S0, BIT(26));

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x33e14);

	rtw8822c_dpk_get_coef(rtwdev, path);

	return result;
}

static void rtw8822c_dpk_cal_gs(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 tmp_gs = 0;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_BYPASS_DPD, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x9);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_INNER_LB, 0x1);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_DPD_CLK, 0xf);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF,
				 0x1066680);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_DPD_EN, 0x1);
	} else {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF,
				 0x1066680);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, BIT_DPD_EN, 0x1);
	}

	if (dpk_info->dpk_bw == DPK_CHANNEL_WIDTH_80) {
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x80001310);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x00001310);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x810000db);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x010000db);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0000b428);
		rtw_write32(rtwdev, REG_DPD_CTL15,
			    0x05020000 | (BIT(path) << 28));
	} else {
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x8200190c);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0200190c);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x8301ee14);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0301ee14);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0000b428);
		rtw_write32(rtwdev, REG_DPD_CTL15,
			    0x05020008 | (BIT(path) << 28));
	}

	rtw_write32_mask(rtwdev, REG_DPD_CTL0, MASKBYTE3, 0x8 | path);

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_CAL_PWR);

	rtw_write32_mask(rtwdev, REG_DPD_CTL15, MASKBYTE3, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_INNER_LB, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);

	if (path == RF_PATH_A)
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF, 0x5b);
	else
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF, 0x5b);

	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x0);

	tmp_gs = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, BIT_RPT_DGAIN);
	tmp_gs = (tmp_gs * 910) >> 10;
	tmp_gs = DIV_ROUND_CLOSEST(tmp_gs, 10);

	if (path == RF_PATH_A)
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF, tmp_gs);
	else
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF, tmp_gs);

	dpk_info->dpk_gs[path] = tmp_gs;
}

static void rtw8822c_dpk_cal_coef1(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 offset[DPK_RF_PATH_NUM] = {0, 0x58};
	u32 i_scaling;
	u8 path;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x0000000c);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);
	rtw_write32(rtwdev, REG_NCTL0, 0x00001148);
	rtw_write32(rtwdev, REG_NCTL0, 0x00001149);

	check_hw_ready(rtwdev, 0x2d9c, MASKBYTE0, 0x55);

	rtw_write8(rtwdev, 0x1b10, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x0000000c);

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		i_scaling = 0x16c00 / dpk_info->dpk_gs[path];

		rtw_write32_mask(rtwdev, 0x1b18 + offset[path], MASKHWORD,
				 i_scaling);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x9);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x1);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x0);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0 + offset[path],
				 BIT(14), 0x0);
	}
}

static void rtw8822c_dpk_on(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DPK_ON);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);

	if (test_bit(path, dpk_info->dpk_path_ok))
		rtw8822c_dpk_cal_gs(rtwdev, path);
}

static bool rtw8822c_dpk_check_pass(struct rtw_dev *rtwdev, bool is_fail,
				    u32 dpk_txagc, u8 path)
{
	bool result;

	if (!is_fail) {
		if (rtw8822c_dpk_coef_read(rtwdev, path))
			result = true;
		else
			result = false;
	} else {
		result = false;
	}

	rtw8822c_dpk_fill_result(rtwdev, dpk_txagc, path, result);

	return result;
}

static void rtw8822c_dpk_result_reset(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		clear_bit(path, dpk_info->dpk_path_ok);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, 0x1b58, 0x0000007f, 0x0);

		dpk_info->dpk_txagc[path] = 0;
		dpk_info->result[path] = 0;
		dpk_info->dpk_gs[path] = 0x5b;
		dpk_info->pre_pwsf[path] = 0;
		dpk_info->thermal_dpk[path] = rtw8822c_dpk_thermal_read(rtwdev,
									path);
	}
}

static void rtw8822c_dpk_calibrate(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 dpk_txagc;
	u8 dpk_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DPK] s%d dpk start\n", path);

	dpk_txagc = rtw8822c_dpk_gainloss(rtwdev, path);

	dpk_fail = rtw8822c_dpk_by_path(rtwdev, dpk_txagc, path);

	if (!rtw8822c_dpk_check_pass(rtwdev, dpk_fail, dpk_txagc, path))
		rtw_err(rtwdev, "failed to do dpk calibration\n");

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[DPK] s%d dpk finish\n", path);

	if (dpk_info->result[path])
		set_bit(path, dpk_info->dpk_path_ok);
}

static void rtw8822c_dpk_path_select(struct rtw_dev *rtwdev)
{
	rtw8822c_dpk_calibrate(rtwdev, RF_PATH_A);
	rtw8822c_dpk_calibrate(rtwdev, RF_PATH_B);
	rtw8822c_dpk_on(rtwdev, RF_PATH_A);
	rtw8822c_dpk_on(rtwdev, RF_PATH_B);
	rtw8822c_dpk_cal_coef1(rtwdev);
}

static void rtw8822c_dpk_enable_disable(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 mask = BIT(15) | BIT(14);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);

	rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_DPD_EN,
			 dpk_info->is_dpk_pwr_on);
	rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, BIT_DPD_EN,
			 dpk_info->is_dpk_pwr_on);

	if (test_bit(RF_PATH_A, dpk_info->dpk_path_ok)) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, mask, 0x0);
		rtw_write8(rtwdev, REG_DPD_CTL0_S0, dpk_info->dpk_gs[RF_PATH_A]);
	}
	if (test_bit(RF_PATH_B, dpk_info->dpk_path_ok)) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, mask, 0x0);
		rtw_write8(rtwdev, REG_DPD_CTL0_S1, dpk_info->dpk_gs[RF_PATH_B]);
	}
}

static void rtw8822c_dpk_reload_data(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 path;

	if (!test_bit(RF_PATH_A, dpk_info->dpk_path_ok) &&
	    !test_bit(RF_PATH_B, dpk_info->dpk_path_ok) &&
	    dpk_info->dpk_ch == 0)
		return;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		if (dpk_info->dpk_band == RTW_BAND_2G)
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f100000);
		else
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f0d0000);

		rtw_write8(rtwdev, REG_DPD_AGC, dpk_info->dpk_txagc[path]);

		rtw8822c_dpk_coef_write(rtwdev, path,
					test_bit(path, dpk_info->dpk_path_ok));

		rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DPK_ON);

		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);

		if (path == RF_PATH_A)
			rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF,
					 dpk_info->dpk_gs[path]);
		else
			rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF,
					 dpk_info->dpk_gs[path]);
	}
	rtw8822c_dpk_cal_coef1(rtwdev);
}

static bool rtw8822c_dpk_reload(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 channel;

	dpk_info->is_reload = false;

	channel = (u8)(rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK) & 0xff);

	if (channel == dpk_info->dpk_ch) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[DPK] DPK reload for CH%d!!\n", dpk_info->dpk_ch);
		rtw8822c_dpk_reload_data(rtwdev);
		dpk_info->is_reload = true;
	}

	return dpk_info->is_reload;
}

static void rtw8822c_do_dpk(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	struct rtw_backup_info bckp[DPK_BB_REG_NUM];
	u32 rf_reg_backup[DPK_RF_REG_NUM][DPK_RF_PATH_NUM];
	u32 bb_reg[DPK_BB_REG_NUM] = {
		0x520, 0x820, 0x824, 0x1c3c, 0x1d58, 0x1864,
		0x4164, 0x180c, 0x410c, 0x186c, 0x416c,
		0x1a14, 0x1e70, 0x80c, 0x1d70, 0x1e7c, 0x18a4, 0x41a4};
	u32 rf_reg[DPK_RF_REG_NUM] = {
		0x0, 0x1a, 0x55, 0x63, 0x87, 0x8f, 0xde};
	u8 path;

	if (!dpk_info->is_dpk_pwr_on) {
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[DPK] Skip DPK due to DPD PWR off\n");
		return;
	} else if (rtw8822c_dpk_reload(rtwdev)) {
		return;
	}

	for (path = RF_PATH_A; path < DPK_RF_PATH_NUM; path++)
		ewma_thermal_init(&dpk_info->avg_thermal[path]);

	rtw8822c_dpk_information(rtwdev);

	rtw8822c_dpk_backup_registers(rtwdev, bb_reg, DPK_BB_REG_NUM, bckp);
	rtw8822c_dpk_backup_rf_registers(rtwdev, rf_reg, rf_reg_backup);

	rtw8822c_dpk_mac_bb_setting(rtwdev);
	rtw8822c_dpk_afe_setting(rtwdev, true);
	rtw8822c_dpk_pre_setting(rtwdev);
	rtw8822c_dpk_result_reset(rtwdev);
	rtw8822c_dpk_path_select(rtwdev);
	rtw8822c_dpk_afe_setting(rtwdev, false);
	rtw8822c_dpk_enable_disable(rtwdev);

	rtw8822c_dpk_reload_rf_registers(rtwdev, rf_reg, rf_reg_backup);
	for (path = 0; path < rtwdev->hal.rf_path_num; path++)
		rtw8822c_dpk_rxbb_dc_cal(rtwdev, path);
	rtw8822c_dpk_restore_registers(rtwdev, DPK_BB_REG_NUM, bckp);
}

static void rtw8822c_phy_calibration(struct rtw_dev *rtwdev)
{
	rtw8822c_rfk_power_save(rtwdev, false);
	rtw8822c_do_gapk(rtwdev);
	rtw8822c_do_iqk(rtwdev);
	rtw8822c_do_dpk(rtwdev);
	rtw8822c_rfk_power_save(rtwdev, true);
}

static void rtw8822c_dpk_track(struct rtw_dev *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 path;
	u8 thermal_value[DPK_RF_PATH_NUM] = {0};
	s8 offset[DPK_RF_PATH_NUM], delta_dpk[DPK_RF_PATH_NUM];

	if (dpk_info->thermal_dpk[0] == 0 && dpk_info->thermal_dpk[1] == 0)
		return;

	for (path = 0; path < DPK_RF_PATH_NUM; path++) {
		thermal_value[path] = rtw8822c_dpk_thermal_read(rtwdev, path);
		ewma_thermal_add(&dpk_info->avg_thermal[path],
				 thermal_value[path]);
		thermal_value[path] =
			ewma_thermal_read(&dpk_info->avg_thermal[path]);
		delta_dpk[path] = dpk_info->thermal_dpk[path] -
				  thermal_value[path];
		offset[path] = delta_dpk[path] -
			       dpk_info->thermal_dpk_delta[path];
		offset[path] &= 0x7f;

		if (offset[path] != dpk_info->pre_pwsf[path]) {
			rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
					 0x8 | (path << 1));
			rtw_write32_mask(rtwdev, 0x1b58, GENMASK(6, 0),
					 offset[path]);
			dpk_info->pre_pwsf[path] = offset[path];
		}
	}
}

#define XCAP_EXTEND(val) ({typeof(val) _v = (val); _v | _v << 7; })
static void rtw8822c_set_crystal_cap_reg(struct rtw_dev *rtwdev, u8 crystal_cap)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;
	u32 val = 0;

	val = XCAP_EXTEND(crystal_cap);
	cfo->crystal_cap = crystal_cap;
	rtw_write32_mask(rtwdev, REG_ANAPAR_XTAL_0, BIT_XCAP_0, val);
}

static void rtw8822c_set_crystal_cap(struct rtw_dev *rtwdev, u8 crystal_cap)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;

	if (cfo->crystal_cap == crystal_cap)
		return;

	rtw8822c_set_crystal_cap_reg(rtwdev, crystal_cap);
}

static void rtw8822c_cfo_tracking_reset(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;

	cfo->is_adjust = true;

	if (cfo->crystal_cap > rtwdev->efuse.crystal_cap)
		rtw8822c_set_crystal_cap(rtwdev, cfo->crystal_cap - 1);
	else if (cfo->crystal_cap < rtwdev->efuse.crystal_cap)
		rtw8822c_set_crystal_cap(rtwdev, cfo->crystal_cap + 1);
}

static void rtw8822c_cfo_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;

	cfo->crystal_cap = rtwdev->efuse.crystal_cap;
	cfo->is_adjust = true;
}

#define REPORT_TO_KHZ(val) ({typeof(val) _v = (val); (_v << 1) + (_v >> 1); })
static s32 rtw8822c_cfo_calc_avg(struct rtw_dev *rtwdev, u8 path_num)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;
	s32 cfo_avg, cfo_path_sum = 0, cfo_rpt_sum;
	u8 i;

	for (i = 0; i < path_num; i++) {
		cfo_rpt_sum = REPORT_TO_KHZ(cfo->cfo_tail[i]);

		if (cfo->cfo_cnt[i])
			cfo_avg = cfo_rpt_sum / cfo->cfo_cnt[i];
		else
			cfo_avg = 0;

		cfo_path_sum += cfo_avg;
	}

	for (i = 0; i < path_num; i++) {
		cfo->cfo_tail[i] = 0;
		cfo->cfo_cnt[i] = 0;
	}

	return cfo_path_sum / path_num;
}

static void rtw8822c_cfo_need_adjust(struct rtw_dev *rtwdev, s32 cfo_avg)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;

	if (!cfo->is_adjust) {
		if (abs(cfo_avg) > CFO_TRK_ENABLE_TH)
			cfo->is_adjust = true;
	} else {
		if (abs(cfo_avg) <= CFO_TRK_STOP_TH)
			cfo->is_adjust = false;
	}

	if (!rtw_coex_disabled(rtwdev)) {
		cfo->is_adjust = false;
		rtw8822c_set_crystal_cap(rtwdev, rtwdev->efuse.crystal_cap);
	}
}

static void rtw8822c_cfo_track(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;
	u8 path_num = rtwdev->hal.rf_path_num;
	s8 crystal_cap = cfo->crystal_cap;
	s32 cfo_avg = 0;

	if (rtwdev->sta_cnt != 1) {
		rtw8822c_cfo_tracking_reset(rtwdev);
		return;
	}

	if (cfo->packet_count == cfo->packet_count_pre)
		return;

	cfo->packet_count_pre = cfo->packet_count;
	cfo_avg = rtw8822c_cfo_calc_avg(rtwdev, path_num);
	rtw8822c_cfo_need_adjust(rtwdev, cfo_avg);

	if (cfo->is_adjust) {
		if (cfo_avg > CFO_TRK_ADJ_TH)
			crystal_cap++;
		else if (cfo_avg < -CFO_TRK_ADJ_TH)
			crystal_cap--;

		crystal_cap = clamp_t(s8, crystal_cap, 0, XCAP_MASK);
		rtw8822c_set_crystal_cap(rtwdev, (u8)crystal_cap);
	}
}

static const struct rtw_phy_cck_pd_reg
rtw8822c_cck_pd_reg[RTW_CHANNEL_WIDTH_40 + 1][RTW_RF_PATH_MAX] = {
	{
		{0x1ac8, 0x00ff, 0x1ad0, 0x01f},
		{0x1ac8, 0xff00, 0x1ad0, 0x3e0}
	},
	{
		{0x1acc, 0x00ff, 0x1ad0, 0x01F00000},
		{0x1acc, 0xff00, 0x1ad0, 0x3E000000}
	},
};

#define RTW_CCK_PD_MAX 255
#define RTW_CCK_CS_MAX 31
#define RTW_CCK_CS_ERR1 27
#define RTW_CCK_CS_ERR2 29
static void
rtw8822c_phy_cck_pd_set_reg(struct rtw_dev *rtwdev,
			    s8 pd_diff, s8 cs_diff, u8 bw, u8 nrx)
{
	u32 pd, cs;

	if (WARN_ON(bw > RTW_CHANNEL_WIDTH_40 || nrx >= RTW_RF_PATH_MAX))
		return;

	pd = rtw_read32_mask(rtwdev,
			     rtw8822c_cck_pd_reg[bw][nrx].reg_pd,
			     rtw8822c_cck_pd_reg[bw][nrx].mask_pd);
	cs = rtw_read32_mask(rtwdev,
			     rtw8822c_cck_pd_reg[bw][nrx].reg_cs,
			     rtw8822c_cck_pd_reg[bw][nrx].mask_cs);
	pd += pd_diff;
	cs += cs_diff;
	if (pd > RTW_CCK_PD_MAX)
		pd = RTW_CCK_PD_MAX;
	if (cs == RTW_CCK_CS_ERR1 || cs == RTW_CCK_CS_ERR2)
		cs++;
	else if (cs > RTW_CCK_CS_MAX)
		cs = RTW_CCK_CS_MAX;
	rtw_write32_mask(rtwdev,
			 rtw8822c_cck_pd_reg[bw][nrx].reg_pd,
			 rtw8822c_cck_pd_reg[bw][nrx].mask_pd,
			 pd);
	rtw_write32_mask(rtwdev,
			 rtw8822c_cck_pd_reg[bw][nrx].reg_cs,
			 rtw8822c_cck_pd_reg[bw][nrx].mask_cs,
			 cs);

	rtw_dbg(rtwdev, RTW_DBG_PHY,
		"is_linked=%d, bw=%d, nrx=%d, cs_ratio=0x%x, pd_th=0x%x\n",
		rtw_is_assoc(rtwdev), bw, nrx, cs, pd);
}

static void rtw8822c_phy_cck_pd_set(struct rtw_dev *rtwdev, u8 new_lvl)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 pd_lvl[CCK_PD_LV_MAX] = {0, 2, 4, 6, 8};
	s8 cs_lvl[CCK_PD_LV_MAX] = {0, 2, 2, 2, 4};
	u8 cur_lvl;
	u8 nrx, bw;

	nrx = (u8)rtw_read32_mask(rtwdev, 0x1a2c, 0x60000);
	bw = (u8)rtw_read32_mask(rtwdev, 0x9b0, 0xc);

	rtw_dbg(rtwdev, RTW_DBG_PHY, "lv: (%d) -> (%d) bw=%d nr=%d cck_fa_avg=%d\n",
		dm_info->cck_pd_lv[bw][nrx], new_lvl, bw, nrx,
		dm_info->cck_fa_avg);

	if (dm_info->cck_pd_lv[bw][nrx] == new_lvl)
		return;

	cur_lvl = dm_info->cck_pd_lv[bw][nrx];

	/* update cck pd info */
	dm_info->cck_fa_avg = CCK_FA_AVG_RESET;

	rtw8822c_phy_cck_pd_set_reg(rtwdev,
				    pd_lvl[new_lvl] - pd_lvl[cur_lvl],
				    cs_lvl[new_lvl] - cs_lvl[cur_lvl],
				    bw, nrx);
	dm_info->cck_pd_lv[bw][nrx] = new_lvl;
}

#define PWR_TRACK_MASK 0x7f
static void rtw8822c_pwrtrack_set(struct rtw_dev *rtwdev, u8 rf_path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	switch (rf_path) {
	case RF_PATH_A:
		rtw_write32_mask(rtwdev, 0x18a0, PWR_TRACK_MASK,
				 dm_info->delta_power_index[rf_path]);
		break;
	case RF_PATH_B:
		rtw_write32_mask(rtwdev, 0x41a0, PWR_TRACK_MASK,
				 dm_info->delta_power_index[rf_path]);
		break;
	default:
		break;
	}
}

static void rtw8822c_pwr_track_stats(struct rtw_dev *rtwdev, u8 path)
{
	u8 thermal_value;

	if (rtwdev->efuse.thermal_meter[path] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, path, RF_T_METER, 0x7e);
	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, path);
}

static void rtw8822c_pwr_track_path(struct rtw_dev *rtwdev,
				    struct rtw_swing_table *swing_table,
				    u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 delta;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, path);
	dm_info->delta_power_index[path] =
		rtw_phy_pwrtrack_get_pwridx(rtwdev, swing_table, path, path,
					    delta);
	rtw8822c_pwrtrack_set(rtwdev, path);
}

static void __rtw8822c_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_swing_table swing_table;
	u8 i;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	for (i = 0; i < rtwdev->hal.rf_path_num; i++)
		rtw8822c_pwr_track_stats(rtwdev, i);
	if (rtw_phy_pwrtrack_need_lck(rtwdev))
		rtw8822c_do_lck(rtwdev);
	for (i = 0; i < rtwdev->hal.rf_path_num; i++)
		rtw8822c_pwr_track_path(rtwdev, &swing_table, i);
}

static void rtw8822c_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (efuse->power_track_type != 0)
		return;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER, BIT(19), 0x01);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER, BIT(19), 0x00);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER, BIT(19), 0x01);

		rtw_write_rf(rtwdev, RF_PATH_B, RF_T_METER, BIT(19), 0x01);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_T_METER, BIT(19), 0x00);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_T_METER, BIT(19), 0x01);

		dm_info->pwr_trk_triggered = true;
		return;
	}

	__rtw8822c_pwr_track(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8822c_adaptivity_init(struct rtw_dev *rtwdev)
{
	rtw_phy_set_edcca_th(rtwdev, RTW8822C_EDCCA_MAX, RTW8822C_EDCCA_MAX);

	/* mac edcca state setting */
	rtw_write32_clr(rtwdev, REG_TX_PTCL_CTRL, BIT_DIS_EDCCA);
	rtw_write32_set(rtwdev, REG_RD_CTRL, BIT_EDCCA_MSK_CNTDOWN_EN);

	/* edcca decistion opt */
	rtw_write32_clr(rtwdev, REG_EDCCA_DECISION, BIT_EDCCA_OPTION);
}

static void rtw8822c_adaptivity(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 l2h, h2l;
	u8 igi;

	igi = dm_info->igi_history[0];
	if (dm_info->edcca_mode == RTW_EDCCA_NORMAL) {
		l2h = max_t(s8, igi + EDCCA_IGI_L2H_DIFF, EDCCA_TH_L2H_LB);
		h2l = l2h - EDCCA_L2H_H2L_DIFF_NORMAL;
	} else {
		if (igi < dm_info->l2h_th_ini - EDCCA_ADC_BACKOFF)
			l2h = igi + EDCCA_ADC_BACKOFF;
		else
			l2h = dm_info->l2h_th_ini;
		h2l = l2h - EDCCA_L2H_H2L_DIFF;
	}

	rtw_phy_set_edcca_th(rtwdev, l2h, h2l);
}

static void rtw8822c_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	size_t words;

	words = (pkt_info->pkt_offset * 8 + chip->tx_pkt_desc_sz) / 2;

	fill_txdesc_checksum_common(txdesc, words);
}

static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8822c[] = {
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x002E,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x002D,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x007F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4) | BIT(7), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8822c[] = {
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0xFF1A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x002E,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x1018,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0074,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0071,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)),
	 (BIT(7) | BIT(6) | BIT(5))},
	{0x0061,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)), 0},
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6)), BIT(7)},
	{0x00EF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6)), BIT(7)},
	{0x1045,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x1064,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8822c[] = {
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x00EF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x1045,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0xFF1A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x30},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8822c[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0081,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7) | BIT(6), 0},
	{0x0090,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0092,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x04},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd * const card_enable_flow_8822c[] = {
	trans_carddis_to_cardemu_8822c,
	trans_cardemu_to_act_8822c,
	NULL
};

static const struct rtw_pwr_seq_cmd * const card_disable_flow_8822c[] = {
	trans_act_to_cardemu_8822c,
	trans_cardemu_to_carddis_8822c,
	NULL
};

static const struct rtw_intf_phy_para usb2_param_8822c[] = {
	{0xFFFF, 0x00,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para usb3_param_8822c[] = {
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen1_param_8822c[] = {
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen2_param_8822c[] = {
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para_table phy_para_table_8822c = {
	.usb2_para	= usb2_param_8822c,
	.usb3_para	= usb3_param_8822c,
	.gen1_para	= pcie_gen1_param_8822c,
	.gen2_para	= pcie_gen2_param_8822c,
	.n_usb2_para	= ARRAY_SIZE(usb2_param_8822c),
	.n_usb3_para	= ARRAY_SIZE(usb2_param_8822c),
	.n_gen1_para	= ARRAY_SIZE(pcie_gen1_param_8822c),
	.n_gen2_para	= ARRAY_SIZE(pcie_gen2_param_8822c),
};

static const struct rtw_hw_reg rtw8822c_dig[] = {
	[0] = { .addr = 0x1d70, .mask = 0x7f },
	[1] = { .addr = 0x1d70, .mask = 0x7f00 },
};

static const struct rtw_ltecoex_addr rtw8822c_ltecoex_addr = {
	.ctrl = LTECOEX_ACCESS_CTRL,
	.wdata = LTECOEX_WRITE_DATA,
	.rdata = LTECOEX_READ_DATA,
};

static const struct rtw_page_table page_table_8822c[] = {
	{64, 64, 64, 64, 1},
	{64, 64, 64, 64, 1},
	{64, 64, 0, 0, 1},
	{64, 64, 64, 0, 1},
	{64, 64, 64, 64, 1},
};

static const struct rtw_rqpn rqpn_table_8822c[] = {
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
};

static const struct rtw_prioq_addrs prioq_addrs_8822c = {
	.prio[RTW_DMA_MAPPING_EXTRA] = {
		.rsvd = REG_FIFOPAGE_INFO_4, .avail = REG_FIFOPAGE_INFO_4 + 2,
	},
	.prio[RTW_DMA_MAPPING_LOW] = {
		.rsvd = REG_FIFOPAGE_INFO_2, .avail = REG_FIFOPAGE_INFO_2 + 2,
	},
	.prio[RTW_DMA_MAPPING_NORMAL] = {
		.rsvd = REG_FIFOPAGE_INFO_3, .avail = REG_FIFOPAGE_INFO_3 + 2,
	},
	.prio[RTW_DMA_MAPPING_HIGH] = {
		.rsvd = REG_FIFOPAGE_INFO_1, .avail = REG_FIFOPAGE_INFO_1 + 2,
	},
	.wsize = true,
};

static const struct rtw_chip_ops rtw8822c_ops = {
	.power_on		= rtw_power_on,
	.power_off		= rtw_power_off,
	.phy_set_param		= rtw8822c_phy_set_param,
	.read_efuse		= rtw8822c_read_efuse,
	.query_phy_status	= query_phy_status,
	.set_channel		= rtw8822c_set_channel,
	.mac_init		= rtw8822c_mac_init,
	.dump_fw_crash		= rtw8822c_dump_fw_crash,
	.read_rf		= rtw_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_mix,
	.set_tx_power_index	= rtw8822c_set_tx_power_index,
	.set_antenna		= rtw8822c_set_antenna,
	.cfg_ldo25		= rtw8822c_cfg_ldo25,
	.false_alarm_statistics	= rtw8822c_false_alarm_statistics,
	.dpk_track		= rtw8822c_dpk_track,
	.phy_calibration	= rtw8822c_phy_calibration,
	.cck_pd_set		= rtw8822c_phy_cck_pd_set,
	.pwr_track		= rtw8822c_pwr_track,
	.config_bfee		= rtw8822c_bf_config_bfee,
	.set_gid_table		= rtw_bf_set_gid_table,
	.cfg_csi_rate		= rtw_bf_cfg_csi_rate,
	.adaptivity_init	= rtw8822c_adaptivity_init,
	.adaptivity		= rtw8822c_adaptivity,
	.cfo_init		= rtw8822c_cfo_init,
	.cfo_track		= rtw8822c_cfo_track,
	.config_tx_path		= rtw8822c_config_tx_path,
	.config_txrx_mode	= rtw8822c_config_trx_mode,
	.fill_txdesc_checksum	= rtw8822c_fill_txdesc_checksum,

	.coex_set_init		= rtw8822c_coex_cfg_init,
	.coex_set_ant_switch	= NULL,
	.coex_set_gnt_fix	= rtw8822c_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= rtw8822c_coex_cfg_gnt_debug,
	.coex_set_rfe_type	= rtw8822c_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8822c_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8822c_coex_cfg_wl_rx_gain,
};

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8822c[] = {
	{0xffffffff, 0xffffffff}, /* case-0 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-5 */
	{0x6a5a5555, 0xaaaaaaaa},
	{0x6a5a56aa, 0x6a5a56aa},
	{0x6a5a5a5a, 0x6a5a5a5a},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-10 */
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0x5a5a5aaa},
	{0x66555555, 0x6aaa5aaa},
	{0x66555555, 0xaaaa5aaa},
	{0x66555555, 0xaaaaaaaa}, /* case-15 */
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x6afa5afa},
	{0xaaffffaa, 0xfafafafa},
	{0xaa5555aa, 0x5a5a5a5a},
	{0xaa5555aa, 0x6a5a5a5a}, /* case-20 */
	{0xaa5555aa, 0xaaaaaaaa},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x55555555},
	{0xffffffff, 0x5a5a5aaa}, /* case-25 */
	{0x55555555, 0x5a5a5a5a},
	{0x55555555, 0xaaaaaaaa},
	{0x55555555, 0x6a5a6a5a},
	{0x66556655, 0x66556655},
	{0x66556aaa, 0x6a5a6aaa}, /*case-30*/
	{0xffffffff, 0x5aaa5aaa},
	{0x56555555, 0x5a5a5aaa},
	{0xdaffdaff, 0xdaffdaff},
	{0xddffddff, 0xddffddff},
};

/* Non-Shared-Antenna Coex Table */
static const struct coex_table_para table_nsant_8822c[] = {
	{0xffffffff, 0xffffffff}, /* case-100 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-105 */
	{0x5afa5afa, 0x5afa5afa},
	{0x55555555, 0xfafafafa},
	{0x66555555, 0xfafafafa},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-110 */
	{0x66555555, 0xaaaaaaaa},
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0xaaaaaaaa},
	{0xffff55ff, 0xffff55ff}, /* case-115 */
	{0xaaffffaa, 0x5afa5afa},
	{0xaaffffaa, 0xaaaaaaaa},
	{0xffffffff, 0xfafafafa},
	{0xffffffff, 0x5afa5afa},
	{0xffffffff, 0xaaaaaaaa}, /* case-120 */
	{0x55ff55ff, 0x5afa5afa},
	{0x55ff55ff, 0xaaaaaaaa},
	{0x55ff55ff, 0x55ff55ff}
};

/* Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_sant_8822c[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-0 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} }, /* case-1 */
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x30, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-5 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-10 */
	{ {0x61, 0x08, 0x03, 0x11, 0x14} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-15 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x20, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-20 */
	{ {0x51, 0x4a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x0c, 0x03, 0x10, 0x54} },
	{ {0x55, 0x08, 0x03, 0x10, 0x54} },
	{ {0x65, 0x10, 0x03, 0x11, 0x10} },
	{ {0x51, 0x10, 0x03, 0x10, 0x51} }, /* case-25 */
	{ {0x51, 0x08, 0x03, 0x10, 0x50} },
	{ {0x61, 0x08, 0x03, 0x11, 0x11} }
};

/* Non-Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_nsant_8822c[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-100 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x30, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-105 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-110 */
	{ {0x61, 0x08, 0x03, 0x11, 0x14} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-115 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x20, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-120 */
	{ {0x51, 0x08, 0x03, 0x10, 0x50} }
};

/* rssi in percentage % (dbm = % - 100) */
static const u8 wl_rssi_step_8822c[] = {60, 50, 44, 30};
static const u8 bt_rssi_step_8822c[] = {8, 15, 20, 25};
static const struct coex_5g_afh_map afh_5g_8822c[] = { {0, 0, 0} };

/* wl_tx_dec_power, bt_tx_dec_power, wl_rx_gain, bt_rx_lna_constrain */
static const struct coex_rf_para rf_para_tx_8822c[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{8, 17, true, 4},
	{7, 18, true, 4},
	{6, 19, true, 4},
	{5, 20, true, 4},
	{0, 21, true, 4}   /* for gamg hid */
};

static const struct coex_rf_para rf_para_rx_8822c[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 16, false, 7}, /* for WL-CPT */
	{3, 24, true, 5},
	{2, 26, true, 5},
	{1, 27, true, 5},
	{0, 28, true, 5},
	{0, 28, true, 5}   /* for gamg hid */
};

static_assert(ARRAY_SIZE(rf_para_tx_8822c) == ARRAY_SIZE(rf_para_rx_8822c));

static const u8
rtw8822c_pwrtrk_5gb_n[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  3,  5,  6,  7,  8,  9, 10,
	 11, 12, 13, 14, 15, 16, 18, 19, 20, 21,
	 22, 23, 24, 25, 26, 27, 28, 29, 30, 32 },
	{ 0,  1,  2,  3,  5,  6,  7,  8,  9, 10,
	 11, 12, 13, 14, 15, 16, 18, 19, 20, 21,
	 22, 23, 24, 25, 26, 27, 28, 29, 30, 32 },
	{ 0,  1,  2,  3,  5,  6,  7,  8,  9, 10,
	 11, 12, 13, 14, 15, 16, 18, 19, 20, 21,
	 22, 23, 24, 25, 26, 27, 28, 29, 30, 32 },
};

static const u8
rtw8822c_pwrtrk_5gb_p[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	 19, 20, 21, 22, 22, 23, 24, 25, 26, 27 },
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	 19, 20, 21, 22, 22, 23, 24, 25, 26, 27 },
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	 19, 20, 21, 22, 22, 23, 24, 25, 26, 27 },
};

static const u8
rtw8822c_pwrtrk_5ga_n[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  4,  5,  6,  7,  8,  9, 10,
	 11, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	 23, 24, 25, 26, 27, 28, 29, 30, 31, 33 },
	{ 0,  1,  2,  4,  5,  6,  7,  8,  9, 10,
	 11, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	 23, 24, 25, 26, 27, 28, 29, 30, 31, 33 },
	{ 0,  1,  2,  4,  5,  6,  7,  8,  9, 10,
	 11, 13, 14, 15, 16, 17, 18, 19, 20, 21,
	 23, 24, 25, 26, 27, 28, 29, 30, 31, 33 },
};

static const u8
rtw8822c_pwrtrk_5ga_p[RTW_PWR_TRK_5G_NUM][RTW_PWR_TRK_TBL_SZ] = {
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 11, 12, 13, 14, 15, 16, 17, 18, 20,
	 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 },
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 11, 12, 13, 14, 15, 16, 17, 18, 20,
	 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 },
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	 10, 11, 12, 13, 14, 15, 16, 17, 18, 20,
	 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 },
};

static const u8 rtw8822c_pwrtrk_2gb_n[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  4,  5,  6,  7,  8,
	 9,  9, 10, 11, 12, 13, 14, 15, 15, 16,
	17, 18, 19, 20, 20, 21, 22, 23, 24, 25
};

static const u8 rtw8822c_pwrtrk_2gb_p[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 12, 13, 14, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 28
};

static const u8 rtw8822c_pwrtrk_2ga_n[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  2,  3,  4,  4,  5,  6,  6,
	 7,  8,  8,  9,  9, 10, 11, 11, 12, 13,
	13, 14, 15, 15, 16, 17, 17, 18, 19, 19
};

static const u8 rtw8822c_pwrtrk_2ga_p[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 25, 26, 27
};

static const u8 rtw8822c_pwrtrk_2g_cck_b_n[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  5,  5,  6,  7,  8,
	 9, 10, 11, 11, 12, 13, 14, 15, 16, 17,
	17, 18, 19, 20, 21, 22, 23, 23, 24, 25
};

static const u8 rtw8822c_pwrtrk_2g_cck_b_p[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29
};

static const u8 rtw8822c_pwrtrk_2g_cck_a_n[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  3,  4,  5,  6,  6,  7,
	 8,  9,  9, 10, 11, 12, 12, 13, 14, 15,
	15, 16, 17, 18, 18, 19, 20, 21, 21, 22
};

static const u8 rtw8822c_pwrtrk_2g_cck_a_p[RTW_PWR_TRK_TBL_SZ] = {
	 0,  1,  2,  3,  4,  5,  5,  6,  7,  8,
	 9, 10, 11, 11, 12, 13, 14, 15, 16, 17,
	18, 18, 19, 20, 21, 22, 23, 24, 24, 25
};

static const struct rtw_pwr_track_tbl rtw8822c_pwr_track_type0_tbl = {
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_1] = rtw8822c_pwrtrk_5gb_n[RTW_PWR_TRK_5G_1],
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_2] = rtw8822c_pwrtrk_5gb_n[RTW_PWR_TRK_5G_2],
	.pwrtrk_5gb_n[RTW_PWR_TRK_5G_3] = rtw8822c_pwrtrk_5gb_n[RTW_PWR_TRK_5G_3],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_1] = rtw8822c_pwrtrk_5gb_p[RTW_PWR_TRK_5G_1],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_2] = rtw8822c_pwrtrk_5gb_p[RTW_PWR_TRK_5G_2],
	.pwrtrk_5gb_p[RTW_PWR_TRK_5G_3] = rtw8822c_pwrtrk_5gb_p[RTW_PWR_TRK_5G_3],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_1] = rtw8822c_pwrtrk_5ga_n[RTW_PWR_TRK_5G_1],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_2] = rtw8822c_pwrtrk_5ga_n[RTW_PWR_TRK_5G_2],
	.pwrtrk_5ga_n[RTW_PWR_TRK_5G_3] = rtw8822c_pwrtrk_5ga_n[RTW_PWR_TRK_5G_3],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_1] = rtw8822c_pwrtrk_5ga_p[RTW_PWR_TRK_5G_1],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_2] = rtw8822c_pwrtrk_5ga_p[RTW_PWR_TRK_5G_2],
	.pwrtrk_5ga_p[RTW_PWR_TRK_5G_3] = rtw8822c_pwrtrk_5ga_p[RTW_PWR_TRK_5G_3],
	.pwrtrk_2gb_n = rtw8822c_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8822c_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8822c_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8822c_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8822c_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8822c_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8822c_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8822c_pwrtrk_2g_cck_a_p,
};

static const struct rtw_rfe_def rtw8822c_rfe_defs[] = {
	[0] = RTW_DEF_RFE(8822c, 0, 0, 0),
	[1] = RTW_DEF_RFE(8822c, 0, 0, 0),
	[2] = RTW_DEF_RFE(8822c, 0, 0, 0),
	[3] = RTW_DEF_RFE(8822c, 0, 0, 0),
	[4] = RTW_DEF_RFE(8822c, 0, 0, 0),
	[5] = RTW_DEF_RFE(8822c, 0, 5, 0),
	[6] = RTW_DEF_RFE(8822c, 0, 0, 0),
};

static const struct rtw_hw_reg_offset rtw8822c_edcca_th[] = {
	[EDCCA_TH_L2H_IDX] = {
		{.addr = 0x84c, .mask = MASKBYTE2}, .offset = 0x80
	},
	[EDCCA_TH_H2L_IDX] = {
		{.addr = 0x84c, .mask = MASKBYTE3}, .offset = 0x80
	},
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8822c = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_GTK_REKEY_FAILURE |
		 WIPHY_WOWLAN_DISCONNECT | WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
		 WIPHY_WOWLAN_NET_DETECT,
	.n_patterns = RTW_MAX_PATTERN_NUM,
	.pattern_max_len = RTW_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
	.max_nd_match_sets = 4,
};
#endif

static const struct rtw_reg_domain coex_info_hw_regs_8822c[] = {
	{0x1860, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x4160, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x1c32, BIT(6), RTW_REG_DOMAIN_MAC8},
	{0x1c38, BIT(28), RTW_REG_DOMAIN_MAC32},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x430, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x434, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x42a, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0x426, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x45e, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x454, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x4c, BIT(24) | BIT(23), RTW_REG_DOMAIN_MAC32},
	{0x64, BIT(0), RTW_REG_DOMAIN_MAC8},
	{0x4c6, BIT(4), RTW_REG_DOMAIN_MAC8},
	{0x40, BIT(5), RTW_REG_DOMAIN_MAC8},
	{0x1, RFREG_MASK, RTW_REG_DOMAIN_RF_B},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x550, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x522, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x953, BIT(1), RTW_REG_DOMAIN_MAC8},
	{0xc50, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
};

const struct rtw_chip_info rtw8822c_hw_spec = {
	.ops = &rtw8822c_ops,
	.id = RTW_CHIP_TYPE_8822C,
	.fw_name = "rtw88/rtw8822c_fw.bin",
	.wlan_cpu = RTW_WCPU_11AC,
	.tx_pkt_desc_sz = 48,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 768,
	.ptct_efuse_size = 124,
	.txff_size = 262144,
	.rxff_size = 24576,
	.fw_rxff_size = 12288,
	.rsvd_drv_pg_num = 16,
	.txgi_factor = 2,
	.is_pwr_by_rate_dec = false,
	.max_power_index = 0x7f,
	.csi_buf_pg_num = 50,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = TX_PAGE_SIZE,
	.dig_min = 0x20,
	.usb_tx_agg_desc_num = 3,
	.hw_feature_report = true,
	.c2h_ra_report_size = 7,
	.old_datarate_fb_limit = false,
	.default_1ss_tx_path = BB_PATH_A,
	.path_div_supported = true,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = BIT(LPS_DEEP_MODE_LCLK) | BIT(LPS_DEEP_MODE_PG),
	.sys_func_en = 0xD8,
	.pwr_on_seq = card_enable_flow_8822c,
	.pwr_off_seq = card_disable_flow_8822c,
	.page_table = page_table_8822c,
	.rqpn_table = rqpn_table_8822c,
	.prioq_addrs = &prioq_addrs_8822c,
	.intf_table = &phy_para_table_8822c,
	.dig = rtw8822c_dig,
	.dig_cck = NULL,
	.rf_base_addr = {0x3c00, 0x4c00},
	.rf_sipi_addr = {0x1808, 0x4108},
	.ltecoex_addr = &rtw8822c_ltecoex_addr,
	.mac_tbl = &rtw8822c_mac_tbl,
	.agc_tbl = &rtw8822c_agc_tbl,
	.bb_tbl = &rtw8822c_bb_tbl,
	.rfk_init_tbl = &rtw8822c_array_mp_cal_init_tbl,
	.rf_tbl = {&rtw8822c_rf_b_tbl, &rtw8822c_rf_a_tbl},
	.rfe_defs = rtw8822c_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8822c_rfe_defs),
	.en_dis_dpd = true,
	.dpd_ratemask = DIS_DPD_RATEALL,
	.iqk_threshold = 8,
	.lck_threshold = 8,
	.bfer_su_max_num = 2,
	.bfer_mu_max_num = 1,
	.rx_ldpc = true,
	.tx_stbc = true,
	.edcca_th = rtw8822c_edcca_th,
	.l2h_th_ini_cs = 60,
	.l2h_th_ini_ad = 45,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_2,

#ifdef CONFIG_PM
	.wow_fw_name = "rtw88/rtw8822c_wow_fw.bin",
	.wowlan_stub = &rtw_wowlan_stub_8822c,
	.max_sched_scan_ssids = 4,
#endif
	.max_scan_ie_len = (RTW_PROBE_PG_CNT - 1) * TX_PAGE_SIZE,
	.coex_para_ver = 0x22020720,
	.bt_desired_ver = 0x20,
	.scbd_support = true,
	.new_scbd10_def = true,
	.ble_hid_profile_support = true,
	.wl_mimo_ps_support = true,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_DBM,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8822c,
	.bt_rssi_step = bt_rssi_step_8822c,
	.table_sant_num = ARRAY_SIZE(table_sant_8822c),
	.table_sant = table_sant_8822c,
	.table_nsant_num = ARRAY_SIZE(table_nsant_8822c),
	.table_nsant = table_nsant_8822c,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8822c),
	.tdma_sant = tdma_sant_8822c,
	.tdma_nsant_num = ARRAY_SIZE(tdma_nsant_8822c),
	.tdma_nsant = tdma_nsant_8822c,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8822c),
	.wl_rf_para_tx = rf_para_tx_8822c,
	.wl_rf_para_rx = rf_para_rx_8822c,
	.bt_afh_span_bw20 = 0x24,
	.bt_afh_span_bw40 = 0x36,
	.afh_5g_num = ARRAY_SIZE(afh_5g_8822c),
	.afh_5g = afh_5g_8822c,

	.coex_info_hw_regs_num = ARRAY_SIZE(coex_info_hw_regs_8822c),
	.coex_info_hw_regs = coex_info_hw_regs_8822c,

	.fw_fifo_addr = {0x780, 0x700, 0x780, 0x660, 0x650, 0x680},
	.fwcd_segs = &rtw8822c_fwcd_segs,
};
EXPORT_SYMBOL(rtw8822c_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8822c_fw.bin");
MODULE_FIRMWARE("rtw88/rtw8822c_wow_fw.bin");

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8822c driver");
MODULE_LICENSE("Dual BSD/GPL");
