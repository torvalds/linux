/**
 * Copyright (c) 2014 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __RSI_BOOTPARAMS_HEADER_H__
#define __RSI_BOOTPARAMS_HEADER_H__

#define CRYSTAL_GOOD_TIME                BIT(0)
#define BOOTUP_MODE_INFO                 BIT(1)
#define WIFI_TAPLL_CONFIGS               BIT(5)
#define WIFI_PLL960_CONFIGS              BIT(6)
#define WIFI_AFEPLL_CONFIGS              BIT(7)
#define WIFI_SWITCH_CLK_CONFIGS          BIT(8)

#define TA_PLL_M_VAL_20                  9
#define TA_PLL_N_VAL_20                  0
#define TA_PLL_P_VAL_20                  4

#define PLL960_M_VAL_20                  0x14
#define PLL960_N_VAL_20                  0
#define PLL960_P_VAL_20                  5

#define UMAC_CLK_40MHZ                   80

#define TA_PLL_M_VAL_40                  9
#define TA_PLL_N_VAL_40                  0
#define TA_PLL_P_VAL_40                  4

#define PLL960_M_VAL_40                  0x14
#define PLL960_N_VAL_40                  0
#define PLL960_P_VAL_40                  5

#define UMAC_CLK_20BW \
	(((TA_PLL_M_VAL_20 + 1) * 40) / \
	 ((TA_PLL_N_VAL_20 + 1) * (TA_PLL_P_VAL_20 + 1)))
#define VALID_20 \
	(WIFI_PLL960_CONFIGS | WIFI_AFEPLL_CONFIGS | WIFI_SWITCH_CLK_CONFIGS)
#define UMAC_CLK_40BW   \
	(((TA_PLL_M_VAL_40 + 1) * 40) / \
	 ((TA_PLL_N_VAL_40 + 1) * (TA_PLL_P_VAL_40 + 1)))
#define VALID_40 \
	(WIFI_PLL960_CONFIGS | WIFI_AFEPLL_CONFIGS | WIFI_SWITCH_CLK_CONFIGS | \
	 WIFI_TAPLL_CONFIGS | CRYSTAL_GOOD_TIME | BOOTUP_MODE_INFO)

/* structure to store configs related to TAPLL programming */
struct tapll_info {
	__le16 pll_reg_1;
	__le16 pll_reg_2;
} __packed;

/* structure to store configs related to PLL960 programming */
struct pll960_info {
	__le16 pll_reg_1;
	__le16 pll_reg_2;
	__le16 pll_reg_3;
} __packed;

/* structure to store configs related to AFEPLL programming */
struct afepll_info {
	__le16 pll_reg;
} __packed;

/* structure to store configs related to pll configs */
struct pll_config {
	struct tapll_info tapll_info_g;
	struct pll960_info pll960_info_g;
	struct afepll_info afepll_info_g;
} __packed;

/* structure to store configs related to UMAC clk programming */
struct switch_clk {
	__le16 switch_clk_info;
	/* If switch_bbp_lmac_clk_reg is set then this value will be programmed
	 * into reg
	 */
	__le16 bbp_lmac_clk_reg_val;
	/* if switch_umac_clk is set then this value will be programmed */
	__le16 umac_clock_reg_config;
	/* if switch_qspi_clk is set then this value will be programmed */
	__le16 qspi_uart_clock_reg_config;
} __packed;

struct device_clk_info {
	struct pll_config pll_config_g;
	struct switch_clk switch_clk_g;
} __packed;

struct bootup_params {
	__le16 magic_number;
	__le16 crystal_good_time;
	__le32 valid;
	__le32 reserved_for_valids;
	__le16 bootup_mode_info;
	/* configuration used for digital loop back */
	__le16 digital_loop_back_params;
	__le16 rtls_timestamp_en;
	__le16 host_spi_intr_cfg;
	struct device_clk_info device_clk_info[3];
	/* ulp buckboost wait time  */
	__le32 buckboost_wakeup_cnt;
	/* pmu wakeup wait time & WDT EN info */
	__le16 pmu_wakeup_wait;
	u8 shutdown_wait_time;
	/* Sleep clock source selection */
	u8 pmu_slp_clkout_sel;
	/* WDT programming values */
	__le32 wdt_prog_value;
	/* WDT soc reset delay */
	__le32 wdt_soc_rst_delay;
	/* dcdc modes configs */
	__le32 dcdc_operation_mode;
	__le32 soc_reset_wait_cnt;
	__le32 waiting_time_at_fresh_sleep;
	__le32 max_threshold_to_avoid_sleep;
	u8 beacon_resedue_alg_en;
} __packed;
#endif
