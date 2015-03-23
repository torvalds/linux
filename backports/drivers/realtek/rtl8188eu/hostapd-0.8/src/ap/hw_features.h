/*
 * hostapd / Hardware feature query and different modes
 * Copyright 2002-2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef HW_FEATURES_H
#define HW_FEATURES_H

#ifdef NEED_AP_MLME
void hostapd_free_hw_features(struct hostapd_hw_modes *hw_features,
			      size_t num_hw_features);
int hostapd_get_hw_features(struct hostapd_iface *iface);
int hostapd_select_hw_mode(struct hostapd_iface *iface);
const char * hostapd_hw_mode_txt(int mode);
int hostapd_hw_get_freq(struct hostapd_data *hapd, int chan);
int hostapd_hw_get_channel(struct hostapd_data *hapd, int freq);
int hostapd_check_ht_capab(struct hostapd_iface *iface);
int hostapd_prepare_rates(struct hostapd_data *hapd,
			  struct hostapd_hw_modes *mode);
#else /* NEED_AP_MLME */
static inline void
hostapd_free_hw_features(struct hostapd_hw_modes *hw_features,
			 size_t num_hw_features)
{
}

static inline int hostapd_get_hw_features(struct hostapd_iface *iface)
{
	return -1;
}

static inline int hostapd_select_hw_mode(struct hostapd_iface *iface)
{
	return -1;
}

static inline const char * hostapd_hw_mode_txt(int mode)
{
	return NULL;
}

static inline int hostapd_hw_get_freq(struct hostapd_data *hapd, int chan)
{
	return -1;
}

static inline int hostapd_check_ht_capab(struct hostapd_iface *iface)
{
	return 0;
}

static inline int hostapd_prepare_rates(struct hostapd_data *hapd,
					struct hostapd_hw_modes *mode)
{
	return 0;
}

#endif /* NEED_AP_MLME */

#endif /* HW_FEATURES_H */
