/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Silicon Labs SI2165 DVB-C/-T Demodulator
 *
 * Copyright (C) 2013-2017 Matthias Schwarzott <zzam@gentoo.org>
 */

#ifndef _DVB_SI2165_PRIV
#define _DVB_SI2165_PRIV

#define SI2165_FIRMWARE_REV_D "dvb-demod-si2165.fw"

struct si2165_config {
	/* i2c addr
	 * possible values: 0x64,0x65,0x66,0x67
	 */
	u8 i2c_addr;

	/* external clock or XTAL */
	u8 chip_mode;

	/* frequency of external clock or xtal in Hz
	 * possible values: 4000000, 16000000, 20000000, 240000000, 27000000
	 */
	u32 ref_freq_hz;

	/* invert the spectrum */
	bool inversion;
};

#define STATISTICS_PERIOD_PKT_COUNT	30000u
#define STATISTICS_PERIOD_BIT_COUNT	(STATISTICS_PERIOD_PKT_COUNT * 204 * 8)

#define REG_CHIP_MODE			0x0000
#define REG_CHIP_REVCODE		0x0023
#define REV_CHIP_TYPE			0x0118
#define REG_CHIP_INIT			0x0050
#define REG_INIT_DONE			0x0054
#define REG_START_INIT			0x0096
#define REG_PLL_DIVL			0x00a0
#define REG_RST_ALL			0x00c0
#define REG_LOCK_TIMEOUT		0x00c4
#define REG_AUTO_RESET			0x00cb
#define REG_OVERSAMP			0x00e4
#define REG_IF_FREQ_SHIFT		0x00e8
#define REG_DVB_STANDARD		0x00ec
#define REG_DSP_CLOCK			0x0104
#define REG_ADC_RI8			0x0123
#define REG_ADC_RI1			0x012a
#define REG_ADC_RI2			0x012b
#define REG_ADC_RI3			0x012c
#define REG_ADC_RI4			0x012d
#define REG_ADC_RI5			0x012e
#define REG_ADC_RI6			0x012f
#define REG_AGC_CRESTF_DBX8		0x0150
#define REG_AGC_UNFREEZE_THR		0x015b
#define REG_AGC2_MIN			0x016e
#define REG_AGC2_KACQ			0x016c
#define REG_AGC2_KLOC			0x016d
#define REG_AGC2_OUTPUT			0x0170
#define REG_AGC2_CLKDIV			0x0171
#define REG_AGC_IF_TRI			0x018b
#define REG_AGC_IF_SLR			0x0190
#define REG_AAF_CRESTF_DBX8		0x01a0
#define REG_ACI_CRESTF_DBX8		0x01c8
#define REG_SWEEP_STEP			0x0232
#define REG_KP_LOCK			0x023a
#define REG_UNKNOWN_24C			0x024c
#define REG_CENTRAL_TAP			0x0261
#define REG_C_N				0x026c
#define REG_EQ_AUTO_CONTROL		0x0278
#define REG_UNKNOWN_27C			0x027c
#define REG_START_SYNCHRO		0x02e0
#define REG_REQ_CONSTELLATION		0x02f4
#define REG_T_BANDWIDTH			0x0308
#define REG_FREQ_SYNC_RANGE		0x030c
#define REG_IMPULSIVE_NOISE_REM		0x031c
#define REG_WDOG_AND_BOOT		0x0341
#define REG_PATCH_VERSION		0x0344
#define REG_ADDR_JUMP			0x0348
#define REG_UNKNOWN_350			0x0350
#define REG_EN_RST_ERROR		0x035c
#define REG_DCOM_CONTROL_BYTE		0x0364
#define REG_DCOM_ADDR			0x0368
#define REG_DCOM_DATA			0x036c
#define REG_RST_CRC			0x0379
#define REG_GP_REG0_LSB			0x0384
#define REG_GP_REG0_MSB			0x0387
#define REG_CRC				0x037a
#define REG_CHECK_SIGNAL		0x03a8
#define REG_CBER_RST			0x0424
#define REG_CBER_BIT			0x0428
#define REG_CBER_ERR			0x0430
#define REG_CBER_AVAIL			0x0434
#define REG_PS_LOCK			0x0440
#define REG_UNCOR_CNT			0x0468
#define REG_BER_RST			0x046c
#define REG_BER_PKT			0x0470
#define REG_BER_BIT			0x0478
#define REG_BER_AVAIL			0x047c
#define REG_FEC_LOCK			0x04e0
#define REG_TS_DATA_MODE		0x04e4
#define REG_TS_CLK_MODE			0x04e5
#define REG_TS_TRI			0x04ef
#define REG_TS_SLR			0x04f4
#define REG_RSSI_ENABLE			0x0641
#define REG_RSSI_PAD_CTRL		0x0646
#define REG_TS_PARALLEL_MODE		0x08f8

#endif /* _DVB_SI2165_PRIV */
