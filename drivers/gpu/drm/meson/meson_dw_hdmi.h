/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#ifndef __MESON_DW_HDMI_H
#define __MESON_DW_HDMI_H

/*
 * Bit 15-10: RW Reserved. Default 1 starting from G12A
 * Bit 9 RW sw_reset_i2c starting from G12A
 * Bit 8 RW sw_reset_axiarb starting from G12A
 * Bit 7 RW Reserved. Default 1, sw_reset_emp starting from G12A
 * Bit 6 RW Reserved. Default 1, sw_reset_flt starting from G12A
 * Bit 5 RW Reserved. Default 1, sw_reset_hdcp22 starting from G12A
 * Bit 4 RW sw_reset_phyif: PHY interface. 1=Apply reset; 0=Release from reset.
 *     Default 1.
 * Bit 3 RW sw_reset_intr: interrupt module. 1=Apply reset;
 *     0=Release from reset.
 *     Default 1.
 * Bit 2 RW sw_reset_mem: KSV/REVOC mem. 1=Apply reset; 0=Release from reset.
 *     Default 1.
 * Bit 1 RW sw_reset_rnd: random number interface to HDCP. 1=Apply reset;
 *     0=Release from reset. Default 1.
 * Bit 0 RW sw_reset_core: connects to IP's ~irstz. 1=Apply reset;
 *     0=Release from reset. Default 1.
 */
#define HDMITX_TOP_SW_RESET                     (0x000)

/*
 * Bit 31 RW free_clk_en: 0=Enable clock gating for power saving; 1= Disable
 * Bit 12 RW i2s_ws_inv:1=Invert i2s_ws; 0=No invert. Default 0.
 * Bit 11 RW i2s_clk_inv: 1=Invert i2s_clk; 0=No invert. Default 0.
 * Bit 10 RW spdif_clk_inv: 1=Invert spdif_clk; 0=No invert. Default 0.
 * Bit 9 RW tmds_clk_inv: 1=Invert tmds_clk; 0=No invert. Default 0.
 * Bit 8 RW pixel_clk_inv: 1=Invert pixel_clk; 0=No invert. Default 0.
 * Bit 7 RW hdcp22_skpclk_en: starting from G12A, 1=enable; 0=disable
 * Bit 6 RW hdcp22_esmclk_en: starting from G12A, 1=enable; 0=disable
 * Bit 5 RW hdcp22_tmdsclk_en: starting from G12A, 1=enable; 0=disable
 * Bit 4 RW cec_clk_en: 1=enable cec_clk; 0=disable. Default 0. Reserved for G12A
 * Bit 3 RW i2s_clk_en: 1=enable i2s_clk; 0=disable. Default 0.
 * Bit 2 RW spdif_clk_en: 1=enable spdif_clk; 0=disable. Default 0.
 * Bit 1 RW tmds_clk_en: 1=enable tmds_clk;  0=disable. Default 0.
 * Bit 0 RW pixel_clk_en: 1=enable pixel_clk; 0=disable. Default 0.
 */
#define HDMITX_TOP_CLK_CNTL                     (0x001)

/*
 * Bit 31:28 RW rxsense_glitch_width: starting from G12A
 * Bit 27:16 RW rxsense_valid_width: starting from G12A
 * Bit 11: 0 RW hpd_valid_width: filter out width <= M*1024.    Default 0.
 * Bit 15:12 RW hpd_glitch_width: filter out glitch <= N.       Default 0.
 */
#define HDMITX_TOP_HPD_FILTER                   (0x002)

/*
 * intr_maskn: MASK_N, one bit per interrupt source.
 *     1=Enable interrupt source; 0=Disable interrupt source. Default 0.
 * [  7] rxsense_fall starting from G12A
 * [  6] rxsense_rise starting from G12A
 * [  5] err_i2c_timeout starting from G12A
 * [  4] hdcp22_rndnum_err
 * [  3] nonce_rfrsh_rise
 * [  2] hpd_fall_intr
 * [  1] hpd_rise_intr
 * [  0] core_intr
 */
#define HDMITX_TOP_INTR_MASKN                   (0x003)

/*
 * Bit 30: 0 RW intr_stat: For each bit, write 1 to manually set the interrupt
 *     bit, read back the interrupt status.
 * Bit    31 R  IP interrupt status
 * Bit     7 RW rxsense_fall starting from G12A
 * Bit     6 RW rxsense_rise starting from G12A
 * Bit     5 RW err_i2c_timeout starting from G12A
 * Bit     2 RW hpd_fall
 * Bit     1 RW hpd_rise
 * Bit     0 RW IP interrupt
 */
#define HDMITX_TOP_INTR_STAT                    (0x004)

/*
 * [7]    rxsense_fall starting from G12A
 * [6]    rxsense_rise starting from G12A
 * [5]    err_i2c_timeout starting from G12A
 * [4]	  hdcp22_rndnum_err
 * [3]	  nonce_rfrsh_rise
 * [2]	  hpd_fall
 * [1]	  hpd_rise
 * [0]	  core_intr_rise
 */
#define HDMITX_TOP_INTR_STAT_CLR                (0x005)

#define HDMITX_TOP_INTR_CORE		BIT(0)
#define HDMITX_TOP_INTR_HPD_RISE	BIT(1)
#define HDMITX_TOP_INTR_HPD_FALL	BIT(2)
#define HDMITX_TOP_INTR_RXSENSE_RISE	BIT(6)
#define HDMITX_TOP_INTR_RXSENSE_FALL	BIT(7)

/*
 * Bit 14:12 RW tmds_sel: 3'b000=Output zero; 3'b001=Output normal TMDS data;
 *     3'b010=Output PRBS data; 3'b100=Output shift pattern. Default 0.
 * Bit 11: 9 RW shift_pttn_repeat: 0=New pattern every clk cycle; 1=New pattern
 *     every 2 clk cycles; ...; 7=New pattern every 8 clk cycles. Default 0.
 * Bit 8 RW shift_pttn_en: 1= Enable shift pattern generator; 0=Disable.
 *     Default 0.
 * Bit 4: 3 RW prbs_pttn_mode: 0=PRBS11; 1=PRBS15; 2=PRBS7; 3=PRBS31. Default 0.
 * Bit 2: 1 RW prbs_pttn_width: 0=idle; 1=output 8-bit pattern;
 *     2=Output 1-bit pattern; 3=output 10-bit pattern. Default 0.
 * Bit 0 RW prbs_pttn_en: 1=Enable PRBS generator; 0=Disable. Default 0.
 */
#define HDMITX_TOP_BIST_CNTL                    (0x006)

/* Bit 29:20 RW shift_pttn_data[59:50]. Default 0. */
/* Bit 19:10 RW shift_pttn_data[69:60]. Default 0. */
/* Bit  9: 0 RW shift_pttn_data[79:70]. Default 0. */
#define HDMITX_TOP_SHIFT_PTTN_012               (0x007)

/* Bit 29:20 RW shift_pttn_data[29:20]. Default 0. */
/* Bit 19:10 RW shift_pttn_data[39:30]. Default 0. */
/* Bit  9: 0 RW shift_pttn_data[49:40]. Default 0. */
#define HDMITX_TOP_SHIFT_PTTN_345               (0x008)

/* Bit 19:10 RW shift_pttn_data[ 9: 0]. Default 0. */
/* Bit  9: 0 RW shift_pttn_data[19:10]. Default 0. */
#define HDMITX_TOP_SHIFT_PTTN_67                (0x009)

/* Bit 25:16 RW tmds_clk_pttn[19:10]. Default 0. */
/* Bit  9: 0 RW tmds_clk_pttn[ 9: 0]. Default 0. */
#define HDMITX_TOP_TMDS_CLK_PTTN_01             (0x00A)

/* Bit 25:16 RW tmds_clk_pttn[39:30]. Default 0. */
/* Bit  9: 0 RW tmds_clk_pttn[29:20]. Default 0. */
#define HDMITX_TOP_TMDS_CLK_PTTN_23             (0x00B)

/*
 * Bit 1 RW shift_tmds_clk_pttn:1=Enable shifting clk pattern,
 * used when TMDS CLK rate = TMDS character rate /4. Default 0.
 * Bit 0 R  Reserved. Default 0.
 * [	1] shift_tmds_clk_pttn
 * [	0] load_tmds_clk_pttn
 */
#define HDMITX_TOP_TMDS_CLK_PTTN_CNTL           (0x00C)

/*
 * Bit 0 RW revocmem_wr_fail: Read back 1 to indicate Host write REVOC MEM
 * failure, write 1 to clear the failure flag.  Default 0.
 */
#define HDMITX_TOP_REVOCMEM_STAT                (0x00D)

/*
 * Bit	   1 R	filtered RxSense status
 * Bit     0 R  filtered HPD status.
 */
#define HDMITX_TOP_STAT0                        (0x00E)

#endif /* __MESON_DW_HDMI_H */
