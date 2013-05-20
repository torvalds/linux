/* alps/ALPS_SW/TRUNK/MAIN/alps/kernel/arch/arm/mach-mt6516/include/mach/fm.h
 *
 * (C) Copyright 2009
 * MediaTek <www.MediaTek.com>
 * William Chung <William.Chung@MediaTek.com>
 *
 * MT6516 AR10x0 FM Radio Driver
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
enum {
    FM_SCAN_SORT_NON = 0,
    FM_SCAN_SORT_UP,
    FM_SCAN_SORT_DOWN,
    FM_SCAN_SORT_MAX
};
//***********************************FM config for customer ***********************************
//RX
#define FMR_RSSI_TH_LONG_MT6620    0x0301      //FM radio long antenna RSSI threshold(11.375dBuV)
#define FMR_RSSI_TH_SHORT_MT6620   0x02E0      //FM radio short antenna RSSI threshold(-1dBuV)
#define FMR_CQI_TH_MT6620          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
//***********************************FM config for engineer ***********************************
//RX
#define FMR_MR_TH_MT6620           0x01BD      //FM radio MR threshold
#if 0
//*****************************************************************************************
//***********************************FM config for customer ***********************************
//*****************************************************************************************
#define FMR_SEEK_SPACE      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define FMR_SCAN_CH_SIZE    40          //FM radio scan max channel size
#define FMR_BAND            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
#define FM_SCAN_SORT_SELECT FM_SCAN_SORT_NON
#define FM_FAKE_CH_NUM      1
#define FM_FAKE_CH_RSSI     40
#define FM_FAKE_CH_1        1075
#define FM_FAKE_CH_2        0
#define FM_FAKE_CH_3        0
#define FM_FAKE_CH_4        0
#define FM_FAKE_CH_5        0

//TX
#define FMTX_PWR_LEVEL_MAX  120

//*****************************************************************************************
#define ADDR_SCAN_TH        0xE0        //scan thrshold register
#define ADDR_CQI_TH         0xE1        //scan CQI register

//TX
#define FMTX_SCAN_HOLE_LOW  923         //92.3MHz~95.4MHz should not show to user
#define FMTX_SCAN_HOLE_HIGH 954         //92.3MHz~95.4MHz should not show to user
//*****************************************************************************************

// errno
#define FM_SUCCESS      0
#define FM_FAILED       1
#define FM_EPARM        2
#define FM_BADSTATUS    3
#define FM_TUNE_FAILED  4
#define FM_SEEK_FAILED  5
#define FM_BUSY         6
#define FM_SCAN_FAILED  7

//max scan chl num
#define FM_MAX_CHL_SIZE FMR_SCAN_CH_SIZE
// auto HiLo
#define FM_AUTO_HILO_OFF    0
#define FM_AUTO_HILO_ON     1

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
#endif
//RX
#define FM_RX_RSSI_TH_LONG_MT6620    -296      //FM radio long antenna RSSI threshold(-4dBuV)
#define FM_RX_RSSI_TH_SHORT_MT6620   -296      //FM radio short antenna RSSI threshold(-4dBuV)
#define FM_RX_DESENSE_RSSI_MT6620   -245      
#define FM_RX_PAMD_TH_MT6620          -12     
#define FM_RX_MR_TH_MT6620           -67      
#define FM_RX_ATDC_TH_MT6620           219      
#define FM_RX_PRX_TH_MT6620           64      
#define FM_RX_SMG_TH_MT6620          6      //FM soft-mute gain threshold
#define FM_RX_DEEMPHASIS_MT6620       0           //0-50us, China Mainland; 1-75us China Taiwan
#define FM_RX_OSC_FREQ_MT6620         0           //0-26MHz; 1-19MHz; 2-24MHz; 3-38.4MHz; 4-40MHz; 5-52MHz  
#if 0
#define FM_RX_RSSI_TH_LONG_MT6620    0xF2D8      //FM radio long antenna RSSI threshold(-4dBuV)
#define FM_RX_RSSI_TH_SHORT_MT6620   0xF2D8      //FM radio short antenna RSSI threshold(-4dBuV)
#define FM_RX_CQI_TH_MT6620          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
#define FM_RX_MR_TH_MT6620           0x01BD      //FM radio MR threshold
#define FM_RX_SMG_TH_MT6620          0x4025      //FM soft-mute gain threshold
#define FM_RX_SEEK_SPACE_MT6620      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define FM_RX_SCAN_CH_SIZE_MT6620    40          //FM radio scan max channel size
#define FM_RX_BAND_MT6620            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special

#define FM_RX_SCAN_SORT_SELECT_MT6620 FM_SCAN_SORT_NON
#define FM_RX_FAKE_CH_NUM_MT6620      1
#define FM_RX_FAKE_CH_RSSI_MT6620     40
#define FM_RX_FAKE_CH_1_MT6620        1075
#define FM_RX_FAKE_CH_2_MT6620        0
#define FM_RX_FAKE_CH_3_MT6620        0
#define FM_RX_FAKE_CH_4_MT6620        0
#define FM_RX_FAKE_CH_5_MT6620        0
#endif
//TX
#define FM_TX_PWR_LEVEL_MAX_MT6620  120  
#define FM_TX_SCAN_HOLE_LOW_MT6620  923         //92.3MHz~95.4MHz should not show to user
#define FM_TX_SCAN_HOLE_HIGH_MT6620 954         //92.3MHz~95.4MHz should not show to user

#endif // __FM_CUST_CFG_H__
