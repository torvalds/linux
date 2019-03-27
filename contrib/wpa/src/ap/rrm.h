/*
 * hostapd / Radio Measurement (RRM)
 * Copyright(c) 2013 - 2016 Intel Mobile Communications GmbH.
 * Copyright(c) 2011 - 2016 Intel Corporation. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef RRM_H
#define RRM_H

/*
 * Max measure request length is 255, -6 of the body we have 249 for the
 * neighbor report elements. Each neighbor report element is at least 2 + 13
 * bytes, so we can't have more than 16 responders in the request.
 */
#define RRM_RANGE_REQ_MAX_RESPONDERS 16

void hostapd_handle_radio_measurement(struct hostapd_data *hapd,
				      const u8 *buf, size_t len);
int hostapd_send_lci_req(struct hostapd_data *hapd, const u8 *addr);
int hostapd_send_range_req(struct hostapd_data *hapd, const u8 *addr,
			   u16 random_interval, u8 min_ap,
			   const u8 *responders, unsigned int n_responders);
void hostapd_clean_rrm(struct hostapd_data *hapd);
int hostapd_send_beacon_req(struct hostapd_data *hapd, const u8 *addr,
			    u8 req_mode, const struct wpabuf *req);
void hostapd_rrm_beacon_req_tx_status(struct hostapd_data *hapd,
				      const struct ieee80211_mgmt *mgmt,
				      size_t len, int ok);

#endif /* RRM_H */
