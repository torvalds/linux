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
#define DFT_TAG         "[FM-MOD-INIT]"

#include "wmt_detect.h"
#include "fm_drv_init.h"

int do_fm_drv_init(int chip_id)
{
	WMT_DETECT_INFO_FUNC("start to do fm module init\n");

#ifdef CONFIG_MTK_FMRADIO
	mtk_wcn_fm_init();
#endif

	WMT_DETECT_INFO_FUNC("finish fm module init\n");
	return 0;
}
