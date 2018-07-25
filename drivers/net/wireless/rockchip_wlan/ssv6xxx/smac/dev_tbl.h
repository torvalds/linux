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

#ifndef _DEV_TBL_H_
#define _DEV_TBL_H_ 
#include "ssv6200_configuration.h"
#include "drv_comm.h"
struct ssv6xxx_dev_table {
    u32 address;
    u32 data;
};
#define ssv6200_phy_tbl phy_setting
#ifdef CONFIG_SSV_CABRIO_E
#define ssv6200_rf_tbl asic_rf_setting
#else
#undef ssv6200_rf_tbl
#define ssv6200_rf_tbl fpga_rf_setting
#endif
#define ACTION_DO_NOTHING 0
#define ACTION_UPDATE_NAV 1
#define ACTION_RESET_NAV 2
#define ACTION_SIGNAL_ACK 3
#define FRAME_ACCEPT 0
#define FRAME_DROP 1
#define SET_DEC_TBL(_type,_mask,_action,_drop) \
    (_type<<9| \
    _mask <<3| \
    _action<<1| \
    _drop)
#ifndef USE_GENERIC_DECI_TBL
 u16 sta_deci_tbl[] =
 {
  SET_DEC_TBL(0x1e, 0x3e, ACTION_RESET_NAV, FRAME_DROP),
  SET_DEC_TBL(0x18, 0x3e, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x1a, 0x3f, ACTION_SIGNAL_ACK, FRAME_DROP),
  SET_DEC_TBL(0x10, 0x38, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x25, 0x3f, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x26, 0x36, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x08, 0x3f, ACTION_DO_NOTHING, FRAME_ACCEPT),
  SET_DEC_TBL(0x05, 0x3f, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x0b, 0x3f, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x01, 0x3d, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x20, 0x30, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_UPDATE_NAV, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_RESET_NAV, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_DROP),
  0x2008,
  0x1001,
#if 0
  0x8408,
  0x1000,
#else
  0x0808,
  0x1040,
#endif
  0x2008,
  0x800E,
  0x0BB8,
  0x2B88,
  0x0800,
 };
 u16 ap_deci_tbl[] =
 {
  SET_DEC_TBL(0x1e, 0x3e, ACTION_RESET_NAV, FRAME_DROP),
  SET_DEC_TBL(0x18, 0x3e, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x1a, 0x3f, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x10, 0x38, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x25, 0x3f, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x26, 0x36, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x08, 0x3f, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x20, 0x30, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_ACCEPT),
  SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_ACCEPT),
  SET_DEC_TBL(0x20, 0x30, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
  SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_UPDATE_NAV, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_RESET_NAV, FRAME_DROP),
  SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_DROP),
  0x2008,
  0x1001,
  0x0888,
  0x1040,
  0x2008,
  0x800E,
  0x0800,
  0x2008,
  0x0800,
 };
#else
u16 generic_deci_tbl[] =
    {
            SET_DEC_TBL(0x1e, 0x3e, ACTION_RESET_NAV, FRAME_DROP),
            SET_DEC_TBL(0x18, 0x3e, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
            SET_DEC_TBL(0x1a, 0x3f, ACTION_DO_NOTHING, FRAME_ACCEPT),
            SET_DEC_TBL(0x10, 0x38, ACTION_DO_NOTHING, FRAME_DROP),
            0,
            0,
            0,
            SET_DEC_TBL(0x05, 0x3f, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
            SET_DEC_TBL(0x0b, 0x3f, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
            SET_DEC_TBL(0x01, 0x3d, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
            SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_ACCEPT),
            SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_ACCEPT),
            SET_DEC_TBL(0x00, 0x00, ACTION_DO_NOTHING, FRAME_DROP),
            SET_DEC_TBL(0x00, 0x00, ACTION_UPDATE_NAV, FRAME_DROP),
            SET_DEC_TBL(0x00, 0x00, ACTION_RESET_NAV, FRAME_DROP),
            SET_DEC_TBL(0x00, 0x00, ACTION_SIGNAL_ACK, FRAME_DROP),
            0x2008,
            0x1001,
            0x0400,
            0x0400,
            0x2000,
            0x800E,
            0x0800,
            0x0B88,
            0x0800,
    };
#endif
#define SET_PHY_INFO(_ctsdur,_ba_rate_idx,_ack_rate_idx,_llength_idx,_llength_enable) \
         (_ctsdur<<16| \
         _ba_rate_idx <<10| \
         _ack_rate_idx<<4| \
         _llength_idx<<1| \
         _llength_enable)
#define SET_PHY_L_LENGTH(_l_ba,_l_rts,_l_cts_ack) (_l_ba<<12|_l_rts<<6 |_l_cts_ack)
#ifdef CONFIG_SSV_CABRIO_E
static u32 phy_info_6051z[] =
{
        0x18000000, 0x18000100, 0x18000200, 0x18000300, 0x18000140,
        0x18000240, 0x18000340, 0x0C000001, 0x0C000101, 0x0C000201,
        0x0C000301, 0x18000401, 0x18000501, 0x18000601, 0x18000701,
        0x0C030002, 0x0C030102, 0x0C030202, 0x18030302, 0x18030402,
        0x18030502, 0x18030602, 0x1C030702, 0x0C030082, 0x0C030182,
        0x0C030282, 0x18030382, 0x18030482, 0x18030582, 0x18030682,
        0x1C030782, 0x0C030042, 0x0C030142, 0x0C030242, 0x18030342,
        0x18030442, 0x18030542, 0x18030642, 0x1C030742
};
#endif
static u32 phy_info_tbl[] =
 {
  0x0C000000, 0x0C000100, 0x0C000200, 0x0C000300, 0x0C000140,
  0x0C000240, 0x0C000340, 0x00000001, 0x00000101, 0x00000201,
  0x00000301, 0x0C000401, 0x0C000501, 0x0C000601, 0x0C000701,
  0x00030002, 0x00030102, 0x00030202, 0x0C030302, 0x0C030402,
  0x0C030502, 0x0C030602, 0x10030702, 0x00030082, 0x00030182,
  0x00030282, 0x0C030382, 0x0C030482, 0x0C030582, 0x0C030682,
  0x10030782, 0x00030042, 0x00030142, 0x00030242, 0x0C030342,
  0x0C030442, 0x0C030542, 0x0C030642, 0x10030742,
  SET_PHY_INFO(314, 0, 0, 0, 0),
  SET_PHY_INFO(258, 0, 1, 0, 0),
  SET_PHY_INFO(223, 0, 1, 0, 0),
  SET_PHY_INFO(213, 0, 1, 0, 0),
  SET_PHY_INFO(162, 0, 4, 0, 0),
  SET_PHY_INFO(127, 0, 4, 0, 0),
  SET_PHY_INFO(117, 0, 4, 0, 0),
  SET_PHY_INFO(60, 7, 7, 0, 0),
  SET_PHY_INFO(52, 7, 7, 0, 0),
  SET_PHY_INFO(48, 9, 9, 0, 0),
  SET_PHY_INFO(44, 9, 9, 0, 0),
  SET_PHY_INFO(44, 11, 11, 0, 0),
  SET_PHY_INFO(40, 11, 11, 0, 0),
  SET_PHY_INFO(40, 11, 11, 0, 0),
  SET_PHY_INFO(40, 11, 11, 0, 0),
  SET_PHY_INFO(76, 7, 7, 0, 1),
  SET_PHY_INFO(64, 9, 9, 1, 1),
  SET_PHY_INFO(60, 9, 9, 2, 1),
  SET_PHY_INFO(60, 11, 11, 3, 1),
  SET_PHY_INFO(56, 11, 11, 4, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(76, 7, 7, 6, 1),
  SET_PHY_INFO(64, 9, 9, 1, 1),
  SET_PHY_INFO(60, 9, 9, 2, 1),
  SET_PHY_INFO(60, 11, 11, 3, 1),
  SET_PHY_INFO(56, 11, 11, 4, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(56, 11, 11, 5, 1),
  SET_PHY_INFO(64, 7, 7, 0, 0),
  SET_PHY_INFO(52, 9, 9, 0, 0),
  SET_PHY_INFO(48, 9, 9, 0, 0),
  SET_PHY_INFO(48, 11, 11, 0, 0),
  SET_PHY_INFO(44, 11, 11, 0, 0),
  SET_PHY_INFO(44, 11, 11, 0, 0),
  SET_PHY_INFO(44, 11, 11, 0, 0),
  SET_PHY_INFO(44, 11, 11, 0, 0),
  SET_PHY_L_LENGTH(50, 38, 35),
  SET_PHY_L_LENGTH(35, 29, 26),
  SET_PHY_L_LENGTH(29, 26, 23),
  SET_PHY_L_LENGTH(26, 23, 23),
  SET_PHY_L_LENGTH(23, 23, 20),
  SET_PHY_L_LENGTH(23, 20, 20),
  SET_PHY_L_LENGTH(47, 38, 35),
  SET_PHY_L_LENGTH( 0, 0, 0),
 };
#endif
