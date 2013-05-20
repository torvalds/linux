/* 
 *
 * (C) Copyright 20011
 * MediaTek <www.MediaTek.com>
 * Hongcheng Xia<Hongcheng.Xia@MediaTek.com>
 *
 * FM Radio Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FM_CUST_CFG_H__
#define __FM_CUST_CFG_H__

//scan sort algorithm 
enum{
    FM_SCAN_SORT_NON = 0,
    FM_SCAN_SORT_UP,
    FM_SCAN_SORT_DOWN,
    FM_SCAN_SORT_MAX
};
/*typedef struct  {
    fm_s32 short_ana_rssi_th;
    fm_s32 long_ana_rssi_th;
    fm_s32 desene_rssi_th;
    fm_s32 pamd_th;
    fm_s32 mr_th;
    fm_s32 atdc_th;
    fm_u32 prx_th;
    fm_u32 atdev_th;
    fm_u16 smg_th;
    fm_u16 deemphasis;
    fm_u16 osc_freq;
}mt6628_fm_rx_cust_cfg;

typedef struct{
    mt6628_fm_rx_cust_cfg rx_cfg;
}mt6628_fm_cust_cfg;
*/
//*****************************************************************************************
//***********************************FM config for customer: start******************************
//*****************************************************************************************
//RX
#define FM_RX_RSSI_TH_LONG_MT6628    -296      //FM radio long antenna RSSI threshold(-4dBuV)
#define FM_RX_RSSI_TH_SHORT_MT6628   -296      //FM radio short antenna RSSI threshold(-4dBuV)
#define FM_RX_DESENSE_RSSI_MT6628   -258      
#define FM_RX_PAMD_TH_MT6628          -12     
#define FM_RX_MR_TH_MT6628           -67      
#define FM_RX_ATDC_TH_MT6628           3496      
#define FM_RX_PRX_TH_MT6628           64      
#define FM_RX_SMG_TH_MT6628          16421      //FM soft-mute gain threshold
#define FM_RX_DEEMPHASIS_MT6628       0           //0-50us, China Mainland; 1-75us China Taiwan
#define FM_RX_OSC_FREQ_MT6628         0           //0-26MHz; 1-19MHz; 2-24MHz; 3-38.4MHz; 4-40MHz; 5-52MHz  
//#define FM_RX_SEEK_SPACE_MT6628      1           //FM radio seek space,1:100KHZ; 2:200KHZ
//#define FM_RX_SCAN_CH_SIZE_MT6628    40          //FM radio scan max channel size
//#define FM_RX_BAND_MT6628            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
//#define FM_RX_SCAN_SORT_SELECT_MT6628 FM_SCAN_SORT_NON
//#define FM_RX_FAKE_CH_NUM_MT6628      1
//#define FM_RX_FAKE_CH_RSSI_MT6628     40
//#define FM_RX_FAKE_CH_1_MT6628        1075
//#define FM_RX_FAKE_CH_2_MT6628        0
//#define FM_RX_FAKE_CH_3_MT6628        0
//#define FM_RX_FAKE_CH_4_MT6628        0
//#define FM_RX_FAKE_CH_5_MT6628        0

//TX
//#define FM_TX_PWR_LEVEL_MAX_MT6628  120  
//#define FM_TX_SCAN_HOLE_LOW_MT6628  923         //92.3MHz~95.4MHz should not show to user
//#define FM_TX_SCAN_HOLE_HIGH_MT6628 954         //92.3MHz~95.4MHz should not show to user


//*****************************************************************************************
//***********************************FM config for customer:end *******************************
//*****************************************************************************************

//#define FM_SEEK_SPACE_MT6628 FM_RX_SEEK_SPACE_MT6628
//max scan chl num
//#define FM_MAX_CHL_SIZ_MT6628E FM_RX_SCAN_CH_SIZE_MT6628
// auto HiLo
#define FM_AUTO_HILO_OFF_MT6628    0
#define FM_AUTO_HILO_ON_MT6628     1


// seek threshold
#define FM_SEEKTH_LEVEL_DEFAULT_MT6628 4

#endif // __FM_CUST_CFG_H__
