/* mt6628_fm.h
 *
 * (C) Copyright 2009 
 * MediaTek <www.MediaTek.com>
 * Hongcheng <hongcheng.xia@MediaTek.com>
 *
 * MT6628 FM Radio Driver --  head file
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
#ifndef __MT6628_FM_H__
#define __MT6628_FM_H__

#include "fm_typedef.h"

//#define FM_PowerOn_with_ShortAntenna
#define MT6628_RSSI_TH_LONG    0xFF01      //FM radio long antenna RSSI threshold(11.375dBuV)
#define MT6628_RSSI_TH_SHORT   0xFEE0      //FM radio short antenna RSSI threshold(-1dBuV)
#define MT6628_CQI_TH          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
#define MT6628_SEEK_SPACE      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define MT6628_SCAN_CH_SIZE    40          //FM radio scan max channel size
#define MT6628_BAND            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
#define MT6628_BAND_FREQ_L     875         //FM radio special band low freq(Default 87.5MHz)
#define MT6628_BAND_FREQ_H     1080        //FM radio special band high freq(Default 108.0MHz)
#define MT6628_DEEMPHASIS_50us TRUE

#define MT6628_SLAVE_ADDR    0xE0	//0x70 7-bit address
#define MT6628_MAX_COUNT     100

#ifdef MTK_FM_50KHZ_SUPPORT
#define MT6628_SCANTBL_SIZE  26 //16*uinit16_t
#else
#define MT6628_SCANTBL_SIZE  16		//16*uinit16_t
#endif

#define AFC_ON  0x01
#if AFC_ON
#define FM_MAIN_CTRL_INIT  0x480
#else
#define FM_MAIN_CTRL_INIT  0x080
#endif

#define ext_clk				//if define ext_clk use external reference clock or mask will use internal
#define MT6628_DEV			"MT6628"   

#endif //end of #ifndef __MT6628_FM_H__

