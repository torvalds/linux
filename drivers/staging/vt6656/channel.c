/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: channel.c
 *
 * Purpose: Channel number mapping
 *
 * Author: Lucas Lin
 *
 * Date: Dec 24, 2004
 *
 *
 *
 * Revision History:
 *      01-18-2005      RobertYu:  remove the for loop searching in ChannelValid,
 *                                 change ChannelRuleTab to lookup-type, reorder table items.
 *
 *
 */

#include <linux/kernel.h>
#include "device.h"
#include "datarate.h"
#include "channel.h"
#include "rf.h"

static SChannelTblElement sChannelTbl[CB_MAX_CHANNEL+1] =
{
  {0,   0,    false},
  {1,   2412, true},
  {2,   2417, true},
  {3,   2422, true},
  {4,   2427, true},
  {5,   2432, true},
  {6,   2437, true},
  {7,   2442, true},
  {8,   2447, true},
  {9,   2452, true},
  {10,  2457, true},
  {11,  2462, true},
  {12,  2467, true},
  {13,  2472, true},
  {14,  2484, true},
  {183, 4915, true}, //15
  {184, 4920, true}, //16
  {185, 4925, true}, //17
  {187, 4935, true}, //18
  {188, 4940, true}, //19
  {189, 4945, true}, //20
  {192, 4960, true}, //21
  {196, 4980, true}, //22
  {7,   5035, true}, //23
  {8,   5040, true}, //24
  {9,   5045, true}, //25
  {11,  5055, true}, //26
  {12,  5060, true}, //27
  {16,  5080, true}, //28
  {34,  5170, true}, //29
  {36,  5180, true}, //30
  {38,  5190, true}, //31
  {40,  5200, true}, //32
  {42,  5210, true}, //33
  {44,  5220, true}, //34
  {46,  5230, true}, //35
  {48,  5240, true}, //36
  {52,  5260, true}, //37
  {56,  5280, true}, //38
  {60,  5300, true}, //39
  {64,  5320, true}, //40
  {100, 5500, true}, //41
  {104, 5520, true}, //42
  {108, 5540, true}, //43
  {112, 5560, true}, //44
  {116, 5580, true}, //45
  {120, 5600, true}, //46
  {124, 5620, true}, //47
  {128, 5640, true}, //48
  {132, 5660, true}, //49
  {136, 5680, true}, //50
  {140, 5700, true}, //51
  {149, 5745, true}, //52
  {153, 5765, true}, //53
  {157, 5785, true}, //54
  {161, 5805, true}, //55
  {165, 5825, true}  //56
};


/************************************************************************
 * Country Channel Valid
 *  Input:  CountryCode, ChannelNum
 *          ChanneIndex is defined as VT3253 MAC channel:
 *              1   = 2.4G channel 1
 *              2   = 2.4G channel 2
 *              ...
 *              14  = 2.4G channel 14
 *              15  = 4.9G channel 183
 *              16  = 4.9G channel 184
 *              .....
 *  Output: true if the specified 5GHz band is allowed to be used.
            False otherwise.
// 4.9G => Ch 183, 184, 185, 187, 188, 189, 192, 196 (Value:15 ~ 22)

// 5G => Ch 7, 8, 9, 11, 12, 16, 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64,
// 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165 (Value 23 ~ 56)
 ************************************************************************/
bool
ChannelValid(unsigned int CountryCode, unsigned int ChannelIndex)
{
    bool    bValid;

    bValid = false;
    /*
     * If Channel Index is invalid, return invalid
     */
    if ((ChannelIndex > CB_MAX_CHANNEL) ||
        (ChannelIndex == 0))
    {
        bValid = false;
        goto exit;
    }

    bValid = sChannelTbl[ChannelIndex].bValid;

exit:
    return (bValid);

} /* end ChannelValid */

void CHvInitChannelTable(struct vnt_private *pDevice)
{
	bool bMultiBand = false;
	int ii;

    for (ii = 1; ii <= CB_MAX_CHANNEL; ii++)
	sChannelTbl[ii].bValid = false;

    switch (pDevice->byRFType) {
        case RF_AL2230:
        case RF_AL2230S:
        case RF_VT3226:
        case RF_VT3226D0:
            bMultiBand = false;
            break;
        case RF_AIROHA7230:
        case RF_VT3342A0:
        default :
            bMultiBand = true;
            break;
    }

        if (bMultiBand == true) {
		for (ii = 0; ii < CB_MAX_CHANNEL; ii++) {
			sChannelTbl[ii+1].bValid = true;
		}
        } else {
		for (ii = 0; ii < CB_MAX_CHANNEL_24G; ii++) {
			sChannelTbl[ii+1].bValid = true;
		}
        }
}

static struct ieee80211_rate vnt_rates_bg[] = {
	{ .bitrate = 10,  .hw_value = RATE_1M },
	{ .bitrate = 20,  .hw_value = RATE_2M },
	{ .bitrate = 55,  .hw_value = RATE_5M },
	{ .bitrate = 110, .hw_value = RATE_11M },
	{ .bitrate = 60,  .hw_value = RATE_6M },
	{ .bitrate = 90,  .hw_value = RATE_9M },
	{ .bitrate = 120, .hw_value = RATE_12M },
	{ .bitrate = 180, .hw_value = RATE_18M },
	{ .bitrate = 240, .hw_value = RATE_24M },
	{ .bitrate = 360, .hw_value = RATE_36M },
	{ .bitrate = 480, .hw_value = RATE_48M },
	{ .bitrate = 540, .hw_value = RATE_54M },
};

static struct ieee80211_rate vnt_rates_a[] = {
	{ .bitrate = 60,  .hw_value = RATE_6M },
	{ .bitrate = 90,  .hw_value = RATE_9M },
	{ .bitrate = 120, .hw_value = RATE_12M },
	{ .bitrate = 180, .hw_value = RATE_18M },
	{ .bitrate = 240, .hw_value = RATE_24M },
	{ .bitrate = 360, .hw_value = RATE_36M },
	{ .bitrate = 480, .hw_value = RATE_48M },
	{ .bitrate = 540, .hw_value = RATE_54M },
};

static struct ieee80211_channel vnt_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 }
};

static struct ieee80211_channel vnt_channels_5ghz[] = {
	{ .center_freq = 4915, .hw_value = 15 },
	{ .center_freq = 4920, .hw_value = 16 },
	{ .center_freq = 4925, .hw_value = 17 },
	{ .center_freq = 4935, .hw_value = 18 },
	{ .center_freq = 4940, .hw_value = 19 },
	{ .center_freq = 4945, .hw_value = 20 },
	{ .center_freq = 4960, .hw_value = 21 },
	{ .center_freq = 4980, .hw_value = 22 },
	{ .center_freq = 5035, .hw_value = 23 },
	{ .center_freq = 5040, .hw_value = 24 },
	{ .center_freq = 5045, .hw_value = 25 },
	{ .center_freq = 5055, .hw_value = 26 },
	{ .center_freq = 5060, .hw_value = 27 },
	{ .center_freq = 5080, .hw_value = 28 },
	{ .center_freq = 5170, .hw_value = 29 },
	{ .center_freq = 5180, .hw_value = 30 },
	{ .center_freq = 5190, .hw_value = 31 },
	{ .center_freq = 5200, .hw_value = 32 },
	{ .center_freq = 5210, .hw_value = 33 },
	{ .center_freq = 5220, .hw_value = 34 },
	{ .center_freq = 5230, .hw_value = 35 },
	{ .center_freq = 5240, .hw_value = 36 },
	{ .center_freq = 5260, .hw_value = 37 },
	{ .center_freq = 5280, .hw_value = 38 },
	{ .center_freq = 5300, .hw_value = 39 },
	{ .center_freq = 5320, .hw_value = 40 },
	{ .center_freq = 5500, .hw_value = 41 },
	{ .center_freq = 5520, .hw_value = 42 },
	{ .center_freq = 5540, .hw_value = 43 },
	{ .center_freq = 5560, .hw_value = 44 },
	{ .center_freq = 5580, .hw_value = 45 },
	{ .center_freq = 5600, .hw_value = 46 },
	{ .center_freq = 5620, .hw_value = 47 },
	{ .center_freq = 5640, .hw_value = 48 },
	{ .center_freq = 5660, .hw_value = 49 },
	{ .center_freq = 5680, .hw_value = 50 },
	{ .center_freq = 5700, .hw_value = 51 },
	{ .center_freq = 5745, .hw_value = 52 },
	{ .center_freq = 5765, .hw_value = 53 },
	{ .center_freq = 5785, .hw_value = 54 },
	{ .center_freq = 5805, .hw_value = 55 },
	{ .center_freq = 5825, .hw_value = 56 }
};

static struct ieee80211_supported_band vnt_supported_2ghz_band = {
	.channels = vnt_channels_2ghz,
	.n_channels = ARRAY_SIZE(vnt_channels_2ghz),
	.bitrates = vnt_rates_bg,
	.n_bitrates = ARRAY_SIZE(vnt_rates_bg),
};

static struct ieee80211_supported_band vnt_supported_5ghz_band = {
	.channels = vnt_channels_5ghz,
	.n_channels = ARRAY_SIZE(vnt_channels_5ghz),
	.bitrates = vnt_rates_a,
	.n_bitrates = ARRAY_SIZE(vnt_rates_a),
};

void vnt_init_bands(struct vnt_private *priv)
{
	struct ieee80211_channel *ch;
	int i;

	switch (priv->byRFType) {
	case RF_AIROHA7230:
	case RF_VT3342A0:
		ch = vnt_channels_5ghz;

		for (i = 0; i < ARRAY_SIZE(vnt_channels_5ghz); i++) {
			ch[i].max_power = VNT_RF_MAX_POWER;
			ch[i].flags = IEEE80211_CHAN_NO_HT40;
		}

		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
						&vnt_supported_5ghz_band;
	/* fallthrough */
	case RF_AL2230:
	case RF_AL2230S:
	case RF_VT3226:
	case RF_VT3226D0:
		ch = vnt_channels_2ghz;

		for (i = 0; i < ARRAY_SIZE(vnt_channels_2ghz); i++) {
			ch[i].max_power = VNT_RF_MAX_POWER;
			ch[i].flags = IEEE80211_CHAN_NO_HT40;
		}

		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
						&vnt_supported_2ghz_band;
		break;
	}
}
