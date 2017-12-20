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
#define DFT_TAG         "[ANT-MOD-INIT]"

#include "wmt_detect.h"
#include "ant_drv_init.h"

int do_ant_drv_init(int chip_id)
{
	int i_ret = -1;

	WMT_DETECT_INFO_FUNC("start to do ANT driver init\n");
	switch (chip_id) {
	case 0x6630:
	case 0x6797:
		i_ret = mtk_wcn_stpant_drv_init();
		WMT_DETECT_INFO_FUNC("finish ANT driver init, i_ret:%d\n", i_ret);
		break;
	default:
		WMT_DETECT_ERR_FUNC("chipid is not 6630,ANT is not supported!\n");
	}
	return i_ret;
}
