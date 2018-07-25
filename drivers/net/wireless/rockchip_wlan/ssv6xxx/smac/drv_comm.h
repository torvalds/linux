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

#ifndef _DRV_COMM_H_
#define _DRV_COMM_H_ 
#define PHY_INFO_TBL1_SIZE 39
#define PHY_INFO_TBL2_SIZE 39
#define PHY_INFO_TBL3_SIZE 8
#define ampdu_fw_rate_info_status_no_use BIT(0)
#define ampdu_fw_rate_info_status_in_use BIT(1)
#define ampdu_fw_rate_info_status_reset BIT(2)
#define SSV_NUM_STA 8
#define SSV_NUM_VIF 2
#define SECURITY_KEY_LEN (32)
enum SSV_CIPHER_E {
    SSV_CIPHER_NONE,
    SSV_CIPHER_WEP40,
    SSV_CIPHER_WEP104,
    SSV_CIPHER_TKIP,
    SSV_CIPHER_CCMP,
    SSV_CIPHER_SMS4,
    SSV_CIPHER_INVALID = (-1)
};
#define ME_NONE 0
#define ME_WEP40 1
#define ME_WEP104 2
#define ME_TKIP 3
#define ME_CCMP 4
#define ME_SMS4 5
struct ssv6xxx_hw_key {
    u8 key[SECURITY_KEY_LEN];
 u32 tx_pn_l;
    u32 tx_pn_h;
 u32 rx_pn_l;
    u32 rx_pn_h;
}__attribute__((packed));
struct ssv6xxx_hw_sta_key {
 u8 pair_key_idx:4;
 u8 group_key_idx:4;
    u8 valid;
 u8 reserve[2];
 struct ssv6xxx_hw_key pair;
}__attribute__((packed));
struct ssv6xxx_hw_sec {
    struct ssv6xxx_hw_key group_key[3];
    struct ssv6xxx_hw_sta_key sta_key[8];
}__attribute__((packed));
#endif
