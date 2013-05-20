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

//*****************************************************************************************
//***********************************FM config for customer: start******************************
//*****************************************************************************************
//RX
#define FM_RX_RSSI_TH_LONG    0xFF01      //FM radio long antenna RSSI threshold(11.375dBuV)
#define FM_RX_RSSI_TH_SHORT   0xFEE0      //FM radio short antenna RSSI threshold(-1dBuV)
#define FM_RX_CQI_TH          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
#define FM_RX_MR_TH           0x01BD      //FM radio MR threshold
#define FM_RX_SMG_TH          0x4025      //FM soft-mute gain threshold
#define FM_RX_SEEK_SPACE      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define FM_RX_SCAN_CH_SIZE    40          //FM radio scan max channel size
#define FM_RX_BAND            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
#define FM_RX_BAND_FREQ_L     875         //FM radio special band low freq(Default 87.5MHz)
#define FM_RX_BAND_FREQ_H     1080        //FM radio special band high freq(Default 108.0MHz)
#define FM_RX_SCAN_SORT_SELECT FM_SCAN_SORT_NON
#define FM_RX_FAKE_CH_NUM      1
#define FM_RX_FAKE_CH_RSSI     40
#define FM_RX_FAKE_CH_1        1075
#define FM_RX_FAKE_CH_2        0
#define FM_RX_FAKE_CH_3        0
#define FM_RX_FAKE_CH_4        0
#define FM_RX_FAKE_CH_5        0
#define FM_RX_DEEMPHASIS       0           //0-50us, China Mainland; 1-75us China Taiwan
#define FM_RX_OSC_FREQ         0           //0-26MHz; 1-19MHz; 2-24MHz; 3-38.4MHz; 4-40MHz; 5-52MHz  

//TX
#define FM_TX_PWR_LEVEL_MAX  120  
#define FM_TX_SCAN_HOLE_LOW  923         //92.3MHz~95.4MHz should not show to user
#define FM_TX_SCAN_HOLE_HIGH 954         //92.3MHz~95.4MHz should not show to user


//*****************************************************************************************
//***********************************FM config for customer:end *******************************
//*****************************************************************************************
// band
#define FM_BAND_UNKNOWN 0
#define FM_BAND_UE      1 // US/Europe band  87.5MHz ~ 108MHz (DEFAULT)
#define FM_BAND_JAPAN   2 // Japan band      76MHz   ~ 90MHz
#define FM_BAND_JAPANW  3 // Japan wideband  76MHZ   ~ 108MHz
#define FM_BAND_SPECIAL 4 // special   band  between 76MHZ   and  108MHz
#define FM_BAND_DEFAULT FM_BAND_UE
#define FM_FREQ_MIN  FM_RX_BAND_FREQ_L
#define FM_FREQ_MAX  FM_RX_BAND_FREQ_H
#define FM_RAIDO_BAND FM_BAND_UE
// space
#define FM_SPACE_UNKNOWN    0
#define FM_SPACE_100K       1
#define FM_SPACE_200K       2
#define FM_SPACE_DEFAULT    FM_RX_SEEK_SPACE
#define FM_SEEK_SPACE FM_RX_SEEK_SPACE
//max scan chl num
#define FM_MAX_CHL_SIZE FM_RX_SCAN_CH_SIZE
// auto HiLo
#define FM_AUTO_HILO_OFF    0
#define FM_AUTO_HILO_ON     1
// seek direction
#define FM_SEEK_UP          0
#define FM_SEEK_DOWN        1

#define FM_CHIP_AR1000 0x1000
#define FM_CHIP_MT5192 0x91
#define FM_CHIP_MT5193 0x92
#define FM_CHIP_MT6616 0x6616
#define FM_CHIP_MT6626 0x6626
#define FM_CHIP_MT6628 0x6628
#define FM_CHIP_MT6620 0x6620
#define FM_CHIP_UNSUPPORTED 0xffff

// seek threshold
#define FM_SEEKTH_LEVEL_DEFAULT 4
#endif // __FM_CUST_CFG_H__