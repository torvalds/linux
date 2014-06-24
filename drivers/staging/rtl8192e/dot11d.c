/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#include "dot11d.h"

struct channel_list {
	u8      Channel[32];
	u8      Len;
};

static struct channel_list ChannelPlan[] = {
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
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(ieee);
	pDot11dInfo->bEnabled = false;

	pDot11dInfo->State = DOT11D_STATE_NONE;
	pDot11dInfo->CountryIeLen = 0;
	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER+1);
	memset(pDot11dInfo->MaxTxPwrDbmList, 0xFF, MAX_CHANNEL_NUMBER+1);
	RESET_CIE_WATCHDOG(ieee);

}
EXPORT_SYMBOL(dot11d_init);

void Dot11d_Channelmap(u8 channel_plan, struct rtllib_device *ieee)
{
	int i, max_chan = 14, min_chan = 1;

	ieee->bGlobalDomain = false;

	if (ChannelPlan[channel_plan].Len != 0) {
		memset(GET_DOT11D_INFO(ieee)->channel_map, 0,
		       sizeof(GET_DOT11D_INFO(ieee)->channel_map));
		for (i = 0; i < ChannelPlan[channel_plan].Len; i++) {
			if (ChannelPlan[channel_plan].Channel[i] < min_chan ||
			    ChannelPlan[channel_plan].Channel[i] > max_chan)
				break;
			GET_DOT11D_INFO(ieee)->channel_map[ChannelPlan
					[channel_plan].Channel[i]] = 1;
		}
	}

	switch (channel_plan) {
	case COUNTRY_CODE_GLOBAL_DOMAIN:
		ieee->bGlobalDomain = true;
		for (i = 12; i <= 14; i++)
			GET_DOT11D_INFO(ieee)->channel_map[i] = 2;
		ieee->IbssStartChnl = 10;
		ieee->ibss_maxjoin_chal = 11;
		break;

	case COUNTRY_CODE_WORLD_WIDE_13:
		for (i = 12; i <= 13; i++)
			GET_DOT11D_INFO(ieee)->channel_map[i] = 2;
		ieee->IbssStartChnl = 10;
		ieee->ibss_maxjoin_chal = 11;
		break;

	default:
		ieee->IbssStartChnl = 1;
		ieee->ibss_maxjoin_chal = 14;
		break;
	}
}
EXPORT_SYMBOL(Dot11d_Channelmap);


void Dot11d_Reset(struct rtllib_device *ieee)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(ieee);
	u32 i;

	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER+1);
	memset(pDot11dInfo->MaxTxPwrDbmList, 0xFF, MAX_CHANNEL_NUMBER+1);
	for (i = 1; i <= 11; i++)
		(pDot11dInfo->channel_map)[i] = 1;
	for (i = 12; i <= 14; i++)
		(pDot11dInfo->channel_map)[i] = 2;
	pDot11dInfo->State = DOT11D_STATE_NONE;
	pDot11dInfo->CountryIeLen = 0;
	RESET_CIE_WATCHDOG(ieee);
}

void Dot11d_UpdateCountryIe(struct rtllib_device *dev, u8 *pTaddr,
			    u16 CoutryIeLen, u8 *pCoutryIe)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 i, j, NumTriples, MaxChnlNum;
	struct chnl_txpow_triple *pTriple;

	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER+1);
	memset(pDot11dInfo->MaxTxPwrDbmList, 0xFF, MAX_CHANNEL_NUMBER+1);
	MaxChnlNum = 0;
	NumTriples = (CoutryIeLen - 3) / 3;
	pTriple = (struct chnl_txpow_triple *)(pCoutryIe + 3);
	for (i = 0; i < NumTriples; i++) {
		if (MaxChnlNum >= pTriple->FirstChnl) {
			printk(KERN_INFO "Dot11d_UpdateCountryIe(): Invalid country IE, skip it........1\n");
			return;
		}
		if (MAX_CHANNEL_NUMBER < (pTriple->FirstChnl +
		    pTriple->NumChnls)) {
			printk(KERN_INFO "Dot11d_UpdateCountryIe(): Invalid country IE, skip it........2\n");
			return;
		}

		for (j = 0; j < pTriple->NumChnls; j++) {
			pDot11dInfo->channel_map[pTriple->FirstChnl + j] = 1;
			pDot11dInfo->MaxTxPwrDbmList[pTriple->FirstChnl + j] =
						 pTriple->MaxTxPowerInDbm;
			MaxChnlNum = pTriple->FirstChnl + j;
		}

		pTriple = (struct chnl_txpow_triple *)((u8 *)pTriple + 3);
	}

	UPDATE_CIE_SRC(dev, pTaddr);

	pDot11dInfo->CountryIeLen = CoutryIeLen;
	memcpy(pDot11dInfo->CountryIeBuf, pCoutryIe, CoutryIeLen);
	pDot11dInfo->State = DOT11D_STATE_LEARNED;
}

u8 DOT11D_GetMaxTxPwrInDbm(struct rtllib_device *dev, u8 Channel)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 MaxTxPwrInDbm = 255;

	if (MAX_CHANNEL_NUMBER < Channel) {
		printk(KERN_INFO "DOT11D_GetMaxTxPwrInDbm(): Invalid Channel\n");
		return MaxTxPwrInDbm;
	}
	if (pDot11dInfo->channel_map[Channel])
		MaxTxPwrInDbm = pDot11dInfo->MaxTxPwrDbmList[Channel];

	return MaxTxPwrInDbm;
}

void DOT11D_ScanComplete(struct rtllib_device *dev)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);

	switch (pDot11dInfo->State) {
	case DOT11D_STATE_LEARNED:
		pDot11dInfo->State = DOT11D_STATE_DONE;
		break;
	case DOT11D_STATE_DONE:
		Dot11d_Reset(dev);
		break;
	case DOT11D_STATE_NONE:
		break;
	}
}

int ToLegalChannel(struct rtllib_device *dev, u8 channel)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 default_chn = 0;
	u32 i;

	for (i = 1; i <= MAX_CHANNEL_NUMBER; i++) {
		if (pDot11dInfo->channel_map[i] > 0) {
			default_chn = i;
			break;
		}
	}

	if (MAX_CHANNEL_NUMBER < channel) {
		printk(KERN_ERR "%s(): Invalid Channel\n", __func__);
		return default_chn;
	}

	if (pDot11dInfo->channel_map[channel] > 0)
		return channel;

	return default_chn;
}
