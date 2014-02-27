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
void ar6k_cfg80211_deinit(AR_SOFTC_DEV_T *arPriv);

void ar6k_cfg80211_scanComplete_event(AR_SOFTC_DEV_T *arPriv, A_STATUS status);

void ar6k_cfg80211_connect_event(AR_SOFTC_DEV_T *arPriv, A_UINT16 channel,
                                A_UINT8 *bssid, A_UINT16 listenInterval,
                                A_UINT16 beaconInterval,NETWORK_TYPE networkType,
                                A_UINT8 beaconIeLen, A_UINT8 assocReqLen,
                                A_UINT8 assocRespLen, A_UINT8 *assocInfo);

void ar6k_cfg80211_disconnect_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 reason,
                                    A_UINT8 *bssid, A_UINT8 assocRespLen,
                                    A_UINT8 *assocInfo, A_UINT16 protocolReasonStatus);

void ar6k_cfg80211_tkip_micerr_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 keyid, A_BOOL ismcast);

#endif /* _AR6K_CFG80211_H_ */






