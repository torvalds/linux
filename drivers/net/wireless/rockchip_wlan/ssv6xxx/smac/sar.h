/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CFG_H_
#define _CFG_H_
#include <linux/kthread.h>

#define SAR_XTAL_INDEX     (0)
#define SAR_TXGAIN_INDEX    (1)
#define THERMAL_MONITOR_TIME (10 * HZ)
#define DEFAULT_CFG_BIN_NAME "/tmp/flash.bin"
#define SEC_CFG_BIN_NAME "/system/etc/wifi/ssv6051/flash.bin"
enum {
    SAR_LVL_LT,
    SAR_LVL_RT,
    SAR_LVL_HT,
    SAR_LVL_INVALID
};

struct flash_thermal_info {
    u32 rt;
    u32 lt;
    u32 ht;
    u8 lt_ts;
    u8 ht_ts;
    u16 reserve;
};
typedef struct t_WIFI_FLASH_CCFG {
    //16bytes
    u16 chip_id;
    u16 sid;
    u32 date;
    u16 version;
    u16 reserve_1;
    u32 reserve_2;
    //16bytes
    struct flash_thermal_info sar_rlh[2];
#if 0
    u32 x_rt;
    u32 x_lt;
    u32 x_ht;
    u8 x_tt_lt;
    u8 x_tt_ht;
    u16 reserve_3;
    //16bytes
    u32 g_rt;
    u32 g_lt;
    u32 g_ht;
    u8 g_tt_lt;
    u8 g_tt_ht;
    u16 reserve_4;
#endif
} WIFI_FLASH_CCFG;


struct t_sar_info {
    u32 lvl;
    u32 value;
    struct flash_thermal_info *p;
};

void thermal_monitor(struct work_struct *work);
int get_flash_info(struct ssv_softc *sc);
void flash_hexdump(void);

#endif
