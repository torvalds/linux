//------------------------------------------------------------------------------
// <copyright file="wlan_recv_beacon.c" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//------------------------------------------------------------------------------
//==============================================================================
// IEEE 802.11 input handling.
//
// Author(s): ="Atheros"
//==============================================================================

#include "a_config.h"
#include "athdefs.h"
#include "a_osapi.h"
#include <wmi.h>
#include <ieee80211.h>
#include <wlan_api.h>

#define IEEE80211_VERIFY_LENGTH(_len, _minlen) do {         \
    if ((_len) < (_minlen)) {                   \
        return A_EINVAL;                         \
    }                               \
} while (0)

#define IEEE80211_VERIFY_ELEMENT(__elem, __maxlen) do {         \
    if ((__elem) == NULL) {                     \
        return A_EINVAL;                         \
    }                               \
    if ((__elem)[1] > (__maxlen)) {                 \
        return A_EINVAL;                         \
    }                               \
} while (0)


/* unaligned little endian access */
#define LE_READ_2(p)                            \
    ((u16)                            \
     ((((u8 *)(p))[0]      ) | (((u8 *)(p))[1] <<  8)))

#define LE_READ_4(p)                            \
    ((u32)                            \
     ((((u8 *)(p))[0]      ) | (((u8 *)(p))[1] <<  8) | \
      (((u8 *)(p))[2] << 16) | (((u8 *)(p))[3] << 24)))


static int __inline
iswpaoui(const u8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((WPA_OUI_TYPE<<24)|WPA_OUI);
}

static int __inline
iswmmoui(const u8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((WMM_OUI_TYPE<<24)|WMM_OUI);
}

/* unused functions for now */
#if 0
static int __inline
iswmmparam(const u8 *frm)
{
    return frm[1] > 5 && frm[6] == WMM_PARAM_OUI_SUBTYPE;
}

static int __inline
iswmminfo(const u8 *frm)
{
    return frm[1] > 5 && frm[6] == WMM_INFO_OUI_SUBTYPE;
}
#endif

static int __inline
isatherosoui(const u8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((ATH_OUI_TYPE<<24)|ATH_OUI);
}

static int __inline
iswscoui(const u8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == ((0x04<<24)|WPA_OUI);
}

int
wlan_parse_beacon(u8 *buf, int framelen, struct ieee80211_common_ie *cie)
{
    u8 *frm, *efrm;
    u8 elemid_ssid = false;

    frm = buf;
    efrm = (u8 *) (frm + framelen);

    /*
     * beacon/probe response frame format
     *  [8] time stamp
     *  [2] beacon interval
     *  [2] capability information
     *  [tlv] ssid
     *  [tlv] supported rates
     *  [tlv] country information
     *  [tlv] parameter set (FH/DS)
     *  [tlv] erp information
     *  [tlv] extended supported rates
     *  [tlv] WMM
     *  [tlv] WPA or RSN
     *  [tlv] Atheros Advanced Capabilities
     */
    IEEE80211_VERIFY_LENGTH(efrm - frm, 12);
    A_MEMZERO(cie, sizeof(*cie));

    cie->ie_tstamp = frm; frm += 8;
    cie->ie_beaconInt = A_LE2CPU16(*(u16 *)frm);  frm += 2;
    cie->ie_capInfo = A_LE2CPU16(*(u16 *)frm);  frm += 2;
    cie->ie_chan = 0;

    while (frm < efrm) {
        switch (*frm) {
        case IEEE80211_ELEMID_SSID:
            if (!elemid_ssid) {
                cie->ie_ssid = frm;
                elemid_ssid = true;
            }
            break;
        case IEEE80211_ELEMID_RATES:
            cie->ie_rates = frm;
            break;
        case IEEE80211_ELEMID_COUNTRY:
            cie->ie_country = frm;
            break;
        case IEEE80211_ELEMID_FHPARMS:
            break;
        case IEEE80211_ELEMID_DSPARMS:
            cie->ie_chan = frm[2];
            break;
        case IEEE80211_ELEMID_TIM:
            cie->ie_tim = frm;
            break;
        case IEEE80211_ELEMID_IBSSPARMS:
            break;
        case IEEE80211_ELEMID_XRATES:
            cie->ie_xrates = frm;
            break;
        case IEEE80211_ELEMID_ERP:
            if (frm[1] != 1) {
                //A_PRINTF("Discarding ERP Element - Bad Len\n");
                return A_EINVAL;
            }
            cie->ie_erp = frm[2];
            break;
        case IEEE80211_ELEMID_RSN:
            cie->ie_rsn = frm;
            break;
        case IEEE80211_ELEMID_HTCAP_ANA:
            cie->ie_htcap = frm;
            break;
        case IEEE80211_ELEMID_HTINFO_ANA:
            cie->ie_htop = frm;
            break;
#ifdef WAPI_ENABLE
		case IEEE80211_ELEMID_WAPI:
            cie->ie_wapi = frm;
            break;
#endif
        case IEEE80211_ELEMID_VENDOR:
            if (iswpaoui(frm)) {
                cie->ie_wpa = frm;
            } else if (iswmmoui(frm)) {
                cie->ie_wmm = frm;
            } else if (isatherosoui(frm)) {
                cie->ie_ath = frm;
            } else if(iswscoui(frm)) {
                cie->ie_wsc = frm;
            }
            break;
        default:
            break;
        }
        frm += frm[1] + 2;
    }
    IEEE80211_VERIFY_ELEMENT(cie->ie_rates, IEEE80211_RATE_MAXSIZE);
    IEEE80211_VERIFY_ELEMENT(cie->ie_ssid, IEEE80211_NWID_LEN);

    return 0;
}
