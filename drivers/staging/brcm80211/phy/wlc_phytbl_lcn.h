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

typedef phytbl_info_t dot11lcnphytbl_info_t;

extern const dot11lcnphytbl_info_t dot11lcnphytbl_rx_gain_info_rev0[];
extern const uint32 dot11lcnphytbl_rx_gain_info_sz_rev0;
extern const dot11lcnphytbl_info_t dot11lcn_sw_ctrl_tbl_info_4313;
extern const dot11lcnphytbl_info_t dot11lcn_sw_ctrl_tbl_info_4313_epa;
extern const dot11lcnphytbl_info_t dot11lcn_sw_ctrl_tbl_info_4313_epa_combo;

extern const dot11lcnphytbl_info_t dot11lcnphytbl_info_rev0[];
extern const uint32 dot11lcnphytbl_info_sz_rev0;

extern const dot11lcnphytbl_info_t dot11lcnphytbl_rx_gain_info_2G_rev2[];
extern const uint32 dot11lcnphytbl_rx_gain_info_2G_rev2_sz;

extern const dot11lcnphytbl_info_t dot11lcnphytbl_rx_gain_info_5G_rev2[];
extern const uint32 dot11lcnphytbl_rx_gain_info_5G_rev2_sz;

extern const dot11lcnphytbl_info_t dot11lcnphytbl_rx_gain_info_extlna_2G_rev2[];

extern const dot11lcnphytbl_info_t dot11lcnphytbl_rx_gain_info_extlna_5G_rev2[];

typedef struct {
	unsigned char gm;
	unsigned char pga;
	unsigned char pad;
	unsigned char dac;
	unsigned char bb_mult;
} lcnphy_tx_gain_tbl_entry;

extern const lcnphy_tx_gain_tbl_entry dot11lcnphy_2GHz_gaintable_rev0[];
extern const lcnphy_tx_gain_tbl_entry dot11lcnphy_2GHz_extPA_gaintable_rev0[];

extern const lcnphy_tx_gain_tbl_entry dot11lcnphy_5GHz_gaintable_rev0[];
