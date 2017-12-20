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
#define DFT_TAG         "[BT-MOD-INIT]"

#include "wmt_detect.h"
#include "bluetooth_drv_init.h"

int do_bluetooth_drv_init(int chip_id)
{
	int i_ret = -1;

#if defined(CONFIG_MTK_COMBO_BT) || defined(CONFIG_MTK_COMBO_BT_HCI)
	WMT_DETECT_INFO_FUNC("start to do bluetooth driver init\n");
	i_ret = mtk_wcn_stpbt_drv_init();
	WMT_DETECT_INFO_FUNC("finish bluetooth driver init, i_ret:%d\n", i_ret);
#else
	WMT_DETECT_INFO_FUNC("CONFIG_MTK_COMBO_BT is not defined\n");
#endif
	return i_ret;
}
