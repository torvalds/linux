/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WLAN-MOD-INIT]"

#include "wmt_detect.h"
#include "wlan_drv_init.h"


int do_wlan_drv_init(int chip_id)
{
	int i_ret = 0;

#ifdef CONFIG_MTK_COMBO_WIFI
	int ret = 0;

	WMT_DETECT_INFO_FUNC("start to do wlan module init 0x%x\n", chip_id);

	/* WMT-WIFI char dev init */
	ret = mtk_wcn_wmt_wifi_init();
	WMT_DETECT_INFO_FUNC("WMT-WIFI char dev init, ret:%d\n", ret);
	i_ret += ret;

	switch (chip_id) {
	case 0x6630:
	case 0x6797:
#ifdef MTK_WCN_WLAN_GEN3
		/* WLAN driver init */
		ret = mtk_wcn_wlan_gen3_init();
		WMT_DETECT_INFO_FUNC("WLAN-GEN3 driver init, ret:%d\n", ret);
		i_ret += ret;
#else
		WMT_DETECT_ERR_FUNC("WLAN-GEN3 driver is not supported, please check CONFIG_MTK_COMBO_CHIP\n");
		i_ret = -1;
#endif
		break;

	default:
#ifdef MTK_WCN_WLAN_GEN2
		/* WLAN driver init */
		ret = mtk_wcn_wlan_gen2_init();
		WMT_DETECT_INFO_FUNC("WLAN-GEN2 driver init, ret:%d\n", ret);
		i_ret += ret;
#else
		WMT_DETECT_ERR_FUNC("WLAN-GEN2 driver is not supported, please check CONFIG_MTK_COMBO_CHIP\n");
		i_ret = -1;
#endif
		break;
	}

	WMT_DETECT_INFO_FUNC("finish wlan module init\n");

#else

	WMT_DETECT_INFO_FUNC("CONFIG_MTK_COMBO_WIFI is not defined\n");

#endif

	return i_ret;
}
