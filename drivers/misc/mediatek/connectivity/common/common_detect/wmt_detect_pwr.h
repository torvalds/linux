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

#ifndef __WMT_DETECT_PWR_H_
#define __WMT_DETECT_PWR_H_

#define MAX_RTC_STABLE_TIME 100
#define MAX_LDO_STABLE_TIME 100
#define MAX_RST_STABLE_TIME 30
#define MAX_OFF_STABLE_TIME 10
#define MAX_ON_STABLE_TIME 30

extern int board_sdio_ctrl(unsigned int sdio_port_num, unsigned int on);
extern int wmt_detect_chip_pwr_ctrl(int on);
extern int wmt_detect_sdio_pwr_ctrl(int on);
extern int wmt_detect_read_ext_cmb_status(void);

#endif
