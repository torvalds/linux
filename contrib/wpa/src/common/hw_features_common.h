/*
 * Common hostapd/wpa_supplicant HW features
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2015, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HW_FEATURES_COMMON_H
#define HW_FEATURES_COMMON_H

#include "drivers/driver.h"

struct hostapd_channel_data * hw_get_channel_chan(struct hostapd_hw_modes *mode,
						  int chan, int *freq);
struct hostapd_channel_data * hw_get_channel_freq(struct hostapd_hw_modes *mode,
						  int freq, int *chan);

int hw_get_freq(struct hostapd_hw_modes *mode, int chan);
int hw_get_chan(struct hostapd_hw_modes *mode, int freq);

int allowed_ht40_channel_pair(struct hostapd_hw_modes *mode, int pri_chan,
			      int sec_chan);
void get_pri_sec_chan(struct wpa_scan_res *bss, int *pri_chan, int *sec_chan);
int check_40mhz_5g(struct hostapd_hw_modes *mode,
		   struct wpa_scan_results *scan_res, int pri_chan,
		   int sec_chan);
int check_40mhz_2g4(struct hostapd_hw_modes *mode,
		    struct wpa_scan_results *scan_res, int pri_chan,
		    int sec_chan);
int hostapd_set_freq_params(struct hostapd_freq_params *data,
			    enum hostapd_hw_mode mode,
			    int freq, int channel, int ht_enabled,
			    int vht_enabled, int sec_channel_offset,
			    int vht_oper_chwidth, int center_segment0,
			    int center_segment1, u32 vht_caps);
void set_disable_ht40(struct ieee80211_ht_capabilities *htcaps,
		      int disabled);
int ieee80211ac_cap_check(u32 hw, u32 conf);

#endif /* HW_FEATURES_COMMON_H */
