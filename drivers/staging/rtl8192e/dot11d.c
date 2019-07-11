/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 ******************************************************************************/
#include "dot11d.h"

struct channel_list {
	u8      channel[32];
	u8      len;
};

static struct channel_list channel_array[] = {
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64,
	  149, 153, 157, 161, 165}, 24},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 11},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56,
	  60, 64}, 21},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52,
	  56, 60, 64}, 22},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52,
	  56, 60, 64}, 22},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52,
	  56, 60, 64}, 22},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52,
	 56, 60, 64}, 22},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52,
	  56, 60, 64}, 21}
};

void dot11d_init(struct rtllib_device *ieee)
{
	struct rt_dot11d_info *dot11d_info = GET_DOT11D_INFO(ieee);

	dot11d_info->enabled = false;

	dot11d_info->state = DOT11D_STATE_NONE;
	dot11d_info->country_len = 0;
	memset(dot11d_info->channel_map, 0, MAX_CHANNEL_NUMBER + 1);
	memset(dot11d_info->max_tx_power_list, 0xFF, MAX_CHANNEL_NUMBER + 1);
	RESET_CIE_WATCHDOG(ieee);
}
EXPORT_SYMBOL(dot11d_init);

void dot11d_channel_map(u8 channel_plan, struct rtllib_device *ieee)
{
	int i, max_chan = 14, min_chan = 1;

	ieee->global_domain = false;

	if (channel_array[channel_plan].len != 0) {
		memset(GET_DOT11D_INFO(ieee)->channel_map, 0,
		       sizeof(GET_DOT11D_INFO(ieee)->channel_map));
		for (i = 0; i < channel_array[channel_plan].len; i++) {
			if (channel_array[channel_plan].channel[i] < min_chan ||
			    channel_array[channel_plan].channel[i] > max_chan)
				break;
			GET_DOT11D_INFO(ieee)->channel_map[channel_array
					[channel_plan].channel[i]] = 1;
		}
	}

	switch (channel_plan) {
	case COUNTRY_CODE_GLOBAL_DOMAIN:
		ieee->global_domain = true;
		for (i = 12; i <= 14; i++)
			GET_DOT11D_INFO(ieee)->channel_map[i] = 2;
		ieee->bss_start_channel = 10;
		ieee->ibss_maxjoin_chal = 11;
		break;

	case COUNTRY_CODE_WORLD_WIDE_13:
		for (i = 12; i <= 13; i++)
			GET_DOT11D_INFO(ieee)->channel_map[i] = 2;
		ieee->bss_start_channel = 10;
		ieee->ibss_maxjoin_chal = 11;
		break;

	default:
		ieee->bss_start_channel = 1;
		ieee->ibss_maxjoin_chal = 14;
		break;
	}
}
EXPORT_SYMBOL(dot11d_channel_map);

void dot11d_reset(struct rtllib_device *ieee)
{
	struct rt_dot11d_info *dot11d_info = GET_DOT11D_INFO(ieee);
	u32 i;

	memset(dot11d_info->channel_map, 0, MAX_CHANNEL_NUMBER + 1);
	memset(dot11d_info->max_tx_power_list, 0xFF, MAX_CHANNEL_NUMBER + 1);
	for (i = 1; i <= 11; i++)
		(dot11d_info->channel_map)[i] = 1;
	for (i = 12; i <= 14; i++)
		(dot11d_info->channel_map)[i] = 2;
	dot11d_info->state = DOT11D_STATE_NONE;
	dot11d_info->country_len = 0;
	RESET_CIE_WATCHDOG(ieee);
}

void dot11d_update_country(struct rtllib_device *dev, u8 *address,
			   u16 country_len, u8 *country)
{
	struct rt_dot11d_info *dot11d_info = GET_DOT11D_INFO(dev);
	u8 i, j, number_of_triples, max_channel_number;
	struct chnl_txpow_triple *triple;

	memset(dot11d_info->channel_map, 0, MAX_CHANNEL_NUMBER + 1);
	memset(dot11d_info->max_tx_power_list, 0xFF, MAX_CHANNEL_NUMBER + 1);
	max_channel_number = 0;
	number_of_triples = (country_len - 3) / 3;
	triple = (struct chnl_txpow_triple *)(country + 3);
	for (i = 0; i < number_of_triples; i++) {
		if (max_channel_number >= triple->first_channel) {
			netdev_info(dev->dev,
				    "%s: Invalid country IE, skip it......1\n",
				    __func__);
			return;
		}
		if (MAX_CHANNEL_NUMBER < (triple->first_channel +
		    triple->num_channels)) {
			netdev_info(dev->dev,
				    "%s: Invalid country IE, skip it......2\n",
				    __func__);
			return;
		}

		for (j = 0; j < triple->num_channels; j++) {
			dot11d_info->channel_map[triple->first_channel + j] = 1;
			dot11d_info->max_tx_power_list[triple->first_channel + j] =
						 triple->max_tx_power;
			max_channel_number = triple->first_channel + j;
		}

		triple = (struct chnl_txpow_triple *)((u8 *)triple + 3);
	}

	UPDATE_CIE_SRC(dev, address);

	dot11d_info->country_len = country_len;
	memcpy(dot11d_info->country_buffer, country, country_len);
	dot11d_info->state = DOT11D_STATE_LEARNED;
}

void dot11d_scan_complete(struct rtllib_device *dev)
{
	struct rt_dot11d_info *dot11d_info = GET_DOT11D_INFO(dev);

	switch (dot11d_info->state) {
	case DOT11D_STATE_LEARNED:
		dot11d_info->state = DOT11D_STATE_DONE;
		break;
	case DOT11D_STATE_DONE:
		dot11d_reset(dev);
		break;
	case DOT11D_STATE_NONE:
		break;
	}
}
