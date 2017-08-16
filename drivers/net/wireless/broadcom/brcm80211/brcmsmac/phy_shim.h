/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * phy_shim.h: stuff defined in phy_shim.c and included only by the phy
 */

#ifndef _BRCM_PHY_SHIM_H_
#define _BRCM_PHY_SHIM_H_

#include "types.h"

#define RADAR_TYPE_NONE		0	/* Radar type None */
#define RADAR_TYPE_ETSI_1	1	/* ETSI 1 Radar type */
#define RADAR_TYPE_ETSI_2	2	/* ETSI 2 Radar type */
#define RADAR_TYPE_ETSI_3	3	/* ETSI 3 Radar type */
#define RADAR_TYPE_ITU_E	4	/* ITU E Radar type */
#define RADAR_TYPE_ITU_K	5	/* ITU K Radar type */
#define RADAR_TYPE_UNCLASSIFIED	6	/* Unclassified Radar type  */
#define RADAR_TYPE_BIN5		7	/* long pulse radar type */
#define RADAR_TYPE_STG2		8	/* staggered-2 radar */
#define RADAR_TYPE_STG3		9	/* staggered-3 radar */
#define RADAR_TYPE_FRA		10	/* French radar */

/* French radar pulse widths */
#define FRA_T1_20MHZ	52770
#define FRA_T2_20MHZ	61538
#define FRA_T3_20MHZ	66002
#define FRA_T1_40MHZ	105541
#define FRA_T2_40MHZ	123077
#define FRA_T3_40MHZ	132004
#define FRA_ERR_20MHZ	60
#define FRA_ERR_40MHZ	120

#define ANTSEL_NA		0 /* No boardlevel selection available */
#define ANTSEL_2x4		1 /* 2x4 boardlevel selection available */
#define ANTSEL_2x3		2 /* 2x3 CB2 boardlevel selection available */

/* Rx Antenna diversity control values */
#define	ANT_RX_DIV_FORCE_0	0	/* Use antenna 0 */
#define	ANT_RX_DIV_FORCE_1	1	/* Use antenna 1 */
#define	ANT_RX_DIV_START_1	2	/* Choose starting with 1 */
#define	ANT_RX_DIV_START_0	3	/* Choose starting with 0 */
#define	ANT_RX_DIV_ENABLE	3	/* APHY bbConfig Enable RX Diversity */
#define ANT_RX_DIV_DEF		ANT_RX_DIV_START_0 /* default antdiv setting */

#define WL_ANT_RX_MAX		2	/* max 2 receive antennas */
#define WL_ANT_HT_RX_MAX	3	/* max 3 receive antennas/cores */
#define WL_ANT_IDX_1		0	/* antenna index 1 */
#define WL_ANT_IDX_2		1	/* antenna index 2 */

/* values for n_preamble_type */
#define BRCMS_N_PREAMBLE_MIXEDMODE	0
#define BRCMS_N_PREAMBLE_GF		1
#define BRCMS_N_PREAMBLE_GF_BRCM          2

#define WL_TX_POWER_RATES_LEGACY	45
#define WL_TX_POWER_MCS20_FIRST	        12
#define WL_TX_POWER_MCS20_NUM	        16
#define WL_TX_POWER_MCS40_FIRST	        28
#define WL_TX_POWER_MCS40_NUM	        17


#define WL_TX_POWER_RATES	       101
#define WL_TX_POWER_CCK_FIRST	       0
#define WL_TX_POWER_CCK_NUM	       4
/* Index for first 20MHz OFDM SISO rate */
#define WL_TX_POWER_OFDM_FIRST	       4
/* Index for first 20MHz OFDM CDD rate */
#define WL_TX_POWER_OFDM20_CDD_FIRST   12
/* Index for first 40MHz OFDM SISO rate */
#define WL_TX_POWER_OFDM40_SISO_FIRST  52
/* Index for first 40MHz OFDM CDD rate */
#define WL_TX_POWER_OFDM40_CDD_FIRST   60
#define WL_TX_POWER_OFDM_NUM	       8
/* Index for first 20MHz MCS SISO rate */
#define WL_TX_POWER_MCS20_SISO_FIRST   20
/* Index for first 20MHz MCS CDD rate */
#define WL_TX_POWER_MCS20_CDD_FIRST    28
/* Index for first 20MHz MCS STBC rate */
#define WL_TX_POWER_MCS20_STBC_FIRST   36
/* Index for first 20MHz MCS SDM rate */
#define WL_TX_POWER_MCS20_SDM_FIRST    44
/* Index for first 40MHz MCS SISO rate */
#define WL_TX_POWER_MCS40_SISO_FIRST   68
/* Index for first 40MHz MCS CDD rate */
#define WL_TX_POWER_MCS40_CDD_FIRST    76
/* Index for first 40MHz MCS STBC rate */
#define WL_TX_POWER_MCS40_STBC_FIRST   84
/* Index for first 40MHz MCS SDM rate */
#define WL_TX_POWER_MCS40_SDM_FIRST    92
#define WL_TX_POWER_MCS_1_STREAM_NUM   8
#define WL_TX_POWER_MCS_2_STREAM_NUM   8
/* Index for 40MHz rate MCS 32 */
#define WL_TX_POWER_MCS_32	       100
#define WL_TX_POWER_MCS_32_NUM	       1

/* sslpnphy specifics */
/* Index for first 20MHz MCS SISO rate */
#define WL_TX_POWER_MCS20_SISO_FIRST_SSN   12

/* struct tx_power::flags bits */
#define WL_TX_POWER_F_ENABLED	1
#define WL_TX_POWER_F_HW	2
#define WL_TX_POWER_F_MIMO	4
#define WL_TX_POWER_F_SISO	8

/* values to force tx/rx chain */
#define BRCMS_N_TXRX_CHAIN0		0
#define BRCMS_N_TXRX_CHAIN1		1

struct brcms_phy;

struct phy_shim_info *wlc_phy_shim_attach(struct brcms_hardware *wlc_hw,
					  struct brcms_info *wl,
					  struct brcms_c_info *wlc);
void wlc_phy_shim_detach(struct phy_shim_info *physhim);

/* PHY to WL utility functions */
struct wlapi_timer *wlapi_init_timer(struct phy_shim_info *physhim,
				     void (*fn)(struct brcms_phy *pi),
				     void *arg, const char *name);
void wlapi_free_timer(struct wlapi_timer *t);
void wlapi_add_timer(struct wlapi_timer *t, uint ms, int periodic);
bool wlapi_del_timer(struct wlapi_timer *t);
void wlapi_intrson(struct phy_shim_info *physhim);
u32 wlapi_intrsoff(struct phy_shim_info *physhim);
void wlapi_intrsrestore(struct phy_shim_info *physhim, u32 macintmask);

void wlapi_bmac_write_shm(struct phy_shim_info *physhim, uint offset, u16 v);
u16 wlapi_bmac_read_shm(struct phy_shim_info *physhim, uint offset);
void wlapi_bmac_mhf(struct phy_shim_info *physhim, u8 idx, u16 mask, u16 val,
		    int bands);
void wlapi_bmac_corereset(struct phy_shim_info *physhim, u32 flags);
void wlapi_suspend_mac_and_wait(struct phy_shim_info *physhim);
void wlapi_switch_macfreq(struct phy_shim_info *physhim, u8 spurmode);
void wlapi_enable_mac(struct phy_shim_info *physhim);
void wlapi_bmac_mctrl(struct phy_shim_info *physhim, u32 mask, u32 val);
void wlapi_bmac_phy_reset(struct phy_shim_info *physhim);
void wlapi_bmac_bw_set(struct phy_shim_info *physhim, u16 bw);
void wlapi_bmac_phyclk_fgc(struct phy_shim_info *physhim, bool clk);
void wlapi_bmac_macphyclk_set(struct phy_shim_info *physhim, bool clk);
void wlapi_bmac_core_phypll_ctl(struct phy_shim_info *physhim, bool on);
void wlapi_bmac_core_phypll_reset(struct phy_shim_info *physhim);
void wlapi_bmac_ucode_wake_override_phyreg_set(struct phy_shim_info *physhim);
void wlapi_bmac_ucode_wake_override_phyreg_clear(struct phy_shim_info *physhim);
void wlapi_bmac_write_template_ram(struct phy_shim_info *physhim, int o,
				   int len, void *buf);
u16 wlapi_bmac_rate_shm_offset(struct phy_shim_info *physhim, u8 rate);
void wlapi_ucode_sample_init(struct phy_shim_info *physhim);
void wlapi_copyfrom_objmem(struct phy_shim_info *physhim, uint, void *buf,
			   int, u32 sel);
void wlapi_copyto_objmem(struct phy_shim_info *physhim, uint, const void *buf,
			 int, u32);

void wlapi_high_update_phy_mode(struct phy_shim_info *physhim, u32 phy_mode);
u16 wlapi_bmac_get_txant(struct phy_shim_info *physhim);

#endif				/* _BRCM_PHY_SHIM_H_ */
