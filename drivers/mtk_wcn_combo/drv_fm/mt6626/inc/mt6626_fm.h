/* mt6628_fm.h
 *
 * (C) Copyright 2009 
 * MediaTek <www.MediaTek.com>
 * Hongcheng <hongcheng.xia@MediaTek.com>
 *
 * MT6626 FM Radio Driver --  head file
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
#ifndef __MT6626_FM_H__
#define __MT6626_FM_H__

#include "fm_typedef.h"

//#define FM_PowerOn_with_ShortAntenna
#define MT6626_RSSI_TH_LONG    0xFF01      //FM radio long antenna RSSI threshold(11.375dBuV)
#define MT6626_RSSI_TH_SHORT   0xFEE0      //FM radio short antenna RSSI threshold(-1dBuV)
#define MT6626_CQI_TH          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
#define MT6626_SEEK_SPACE      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define MT6626_SCAN_CH_SIZE    40          //FM radio scan max channel size
#define MT6626_BAND            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
#define MT6626_BAND_FREQ_L     875         //FM radio special band low freq(Default 87.5MHz)
#define MT6626_BAND_FREQ_H     1080        //FM radio special band high freq(Default 108.0MHz)
#define MT6626_DEEMPHASIS_50us TRUE

//customer need customize the I2C port
#ifdef MT6516
#define MT6626_I2C_PORT   2
#else 
#define MT6626_I2C_PORT   0
#endif

#define MT6626_SLAVE_ADDR    0xE0	//0x70 7-bit address
#define MT6626_MAX_COUNT     100
#define MT6626_SCANTBL_SIZE  16		//16*uinit16_t

#define AFC_ON  0x01
#if AFC_ON
#define FM_MAIN_CTRL_INIT  0x480
#else
#define FM_MAIN_CTRL_INIT  0x080
#endif

//FM_MAIN_EXTINTRMASK
#define FM_EXT_STC_DONE_MASK 0x01
#define FM_EXT_RDS_MASK      0x20

#define MT6626_FM_STC_DONE_TIMEOUT 12  //second

//FM_MAIN_CHANDETSTAT
#define FM_MAIN_CHANDET_MASK   0x3FF0  // D4~D13 in address 6FH
#define FM_MAIN_CHANDET_SHIFT  0x04
#define FM_HOST_CHAN	0x3FF0

//FM_MAIN_CFG1(0x36) && FM_MAIN_CFG2(0x37)
#define MT6626_FM_SEEK_UP       0x0
#define MT6626_FM_SEEK_DOWN     0x01
#define MT6626_FM_SCAN_UP       0x0
#define MT6626_FM_SCAN_DOWN     0x01
#define MT6626_FM_SPACE_INVALID 0x0
#define MT6626_FM_SPACE_50K     0x01
#define MT6626_FM_SPACE_100K    0x02
#define MT6626_FM_SPACE_200K    0x04 

#define ext_clk				//if define ext_clk use external reference clock or mask will use internal
#define MT6626_DEV			"MT6626"   

#endif //end of #ifndef __MT6626_FM_H__

