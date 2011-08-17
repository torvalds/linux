//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _AR6K_CFG80211_H_
#define _AR6K_CFG80211_H_

struct wireless_dev *ar6k_cfg80211_init(struct device *dev);
void ar6k_cfg80211_deinit(struct ar6_softc *ar);

void ar6k_cfg80211_scanComplete_event(struct ar6_softc *ar, int status);

void ar6k_cfg80211_connect_event(struct ar6_softc *ar, u16 channel,
                                u8 *bssid, u16 listenInterval,
                                u16 beaconInterval,NETWORK_TYPE networkType,
                                u8 beaconIeLen, u8 assocReqLen,
                                u8 assocRespLen, u8 *assocInfo);

void ar6k_cfg80211_disconnect_event(struct ar6_softc *ar, u8 reason,
                                    u8 *bssid, u8 assocRespLen,
                                    u8 *assocInfo, u16 protocolReasonStatus);

void ar6k_cfg80211_tkip_micerr_event(struct ar6_softc *ar, u8 keyid, bool ismcast);

#ifdef CONFIG_NL80211_TESTMODE
void ar6000_testmode_rx_report_event(struct ar6_softc *ar, void *buf,
				     int buf_len);
#else
static inline void ar6000_testmode_rx_report_event(struct ar6_softc *ar,
						   void *buf, int buf_len) 
{
}
#endif


#endif /* _AR6K_CFG80211_H_ */






