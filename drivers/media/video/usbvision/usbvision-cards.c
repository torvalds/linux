/* 
 * USBVISION.H
 *  usbvision header file
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/list.h>
#include <linux/i2c.h>
#include <media/v4l2-dev.h>
#include <media/tuner.h>
#include "usbvision.h"

/* Supported Devices: A table for usbvision.c*/

struct usbvision_device_data_st  usbvision_device_data[] = {
	{0xFFF0, 0xFFF0, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Custom Dummy USBVision Device"},  
	{0x0A6F, 0x0400, -1, CODEC_SAA7113, 4, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "Xanboo"}, 
	{0x050D, 0x0208, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Belkin USBView II"},
	{0x0571, 0x0002,  0, CODEC_SAA7111, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1, -1, -1,  7, "echoFX InterView Lite"}, 
	{0x0573, 0x0003, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "USBGear USBG-V1 resp. HAMA USB"},
	{0x0573, 0x0400, -1, CODEC_SAA7113, 4, V4L2_STD_NTSC,  0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "D-Link V100"},
	{0x0573, 0x2000, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1, -1, -1, -1, "X10 USB Camera"},
	{0x0573, 0x2d00, -1, CODEC_SAA7111, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1, -1,  3,  7, "Osprey 50"}, 
	{0x0573, 0x2d01, -1, CODEC_SAA7113, 2, V4L2_STD_NTSC,	 0, 0, 1, 0, 0,			         -1, -1,  0,  3,  7, "Hauppauge USB-Live Model 600"},
	{0x0573, 0x2101, -1, CODEC_SAA7113, 2, V4L2_STD_PAL, 	 2, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Zoran Co. PMD (Nogatech) AV-grabber Manhattan"},
	{0x0573, 0x4100, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "Nogatech USB-TV (NTSC) FM"},
    	{0x0573, 0x4110, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "PNY USB-TV (NTSC) FM"}, 
	{0x0573, 0x4450,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "PixelView PlayTv-USB PRO (PAL) FM"},
	{0x0573, 0x4550,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "ZTV ZT-721 2.4GHz USB A/V Receiver"},
	{0x0573, 0x4d00, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, 20, -1, "Hauppauge WinTv-USB USA"},
	{0x0573, 0x4d01, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB"},
	{0x0573, 0x4d02, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (NTSC)"},
	{0x0573, 0x4d03, -1, CODEC_SAA7111, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (SECAM) "},
	{0x0573, 0x4d10, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (NTSC) FM"},
	{0x0573, 0x4d11, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (PAL) FM"},
	{0x0573, 0x4d12, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Hauppauge WinTv-USB (PAL) FM"},
	{0x0573, 0x4d2a,  0, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv USB (NTSC) FM Model 602 40201 Rev B285"}, 
	{0x0573, 0x4d2b,  0, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv USB (NTSC) FM Model 602 40201 Rev B282"},
	{0x0573, 0x4d2c,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_FM1216ME_MK3, -1, -1,  0,  3,  7, "Hauppauge WinTv USB (PAL/SECAM) 40209 Rev E1A5"}, 
	{0x0573, 0x4d20,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL) FM Model 40201 Rev B226"},
	{0x0573, 0x4d21,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL)"},
	{0x0573, 0x4d22,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB II (PAL) MODEL 566"},
	{0x0573, 0x4d23, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) 4D23"},
	{0x0573, 0x4d25, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) Model 40209 Rev B234"},
	{0x0573, 0x4d26, -1, CODEC_SAA7113, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1,  0,  3,  7, "Hauppauge WinTv-USB (SECAM) Model 40209 Rev B243"},
	{0x0573, 0x4d27, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_ALPS_TSBE1_PAL,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40204 Rev B281"},
	{0x0573, 0x4d28, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_ALPS_TSBE1_PAL,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40204 Rev B283"}, 
	{0x0573, 0x4d29, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB Model 40205 Rev B298"}, 
	{0x0573, 0x4d30, -1, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1,  0,  3,  7, "Hauppauge WinTv-USB FM Model 40211 Rev B123"},
	{0x0573, 0x4d31,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 568"},
        {0x0573, 0x4d32,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 573"},
	{0x0573, 0x4d35,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_MICROTUNE_4049FM5,    -1, -1,  0,  3,  7, "Hauppauge WinTv-USB III (PAL) FM Model 40219 Rev B252"},
	{0x0573, 0x4d37,  0, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_FM1216ME_MK3, -1, -1,  0,  3,  7, "Hauppauge WinTV USB device Model 40219 Rev E189"},
	{0x0768, 0x0006, -1, CODEC_SAA7113, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1,  5,  5, -1, "Camtel Technology USB TV Genie Pro FM Model TVB330"},
	{0x07d0, 0x0001, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Digital Video Creator I"},
	{0x07d0, 0x0002, -1, CODEC_SAA7111, 2, V4L2_STD_NTSC,  0, 0, 1, 0, 0,   		         -1, -1, 82, 20,  7, "Global Village GV-007 (NTSC)"},
	{0x07d0, 0x0003,  0, CODEC_SAA7113, 2, V4L2_STD_NTSC,  0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-50 Rev 1 (NTSC)"},
	{0x07d0, 0x0004,  0, CODEC_SAA7113, 2, V4L2_STD_PAL,   0, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-80 Rev 1 (PAL)"},
	{0x07d0, 0x0005,  0, CODEC_SAA7113, 2, V4L2_STD_SECAM, 0, 0, 1, 0, 0,	   		         -1, -1,  0,  3,  7, "Dazzle Fusion Model DVC-90 Rev 1 (SECAM)"}, 
        {0x2304, 0x010d, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 0, 0, 1, TUNER_TEMIC_4066FY5_PAL_I,  -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (PAL)"},
	{0x2304, 0x0109, -1, CODEC_SAA7111, 3, V4L2_STD_SECAM, 1, 0, 1, 1, TUNER_PHILIPS_SECAM,        -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (SECAM)"},
	{0x2304, 0x0110, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_PHILIPS_PAL,          -1, -1,128, 23, -1, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0111, -1, CODEC_SAA7111, 3, V4L2_STD_PAL,   1, 0, 1, 1, TUNER_PHILIPS_PAL,          -1, -1, -1, -1, -1, "Miro PCTV USB"},
	{0x2304, 0x0112, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Pinnacle Studio PCTV USB (NTSC) FM"},
	{0x2304, 0x0210, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0212, -1, CODEC_SAA7111, 3, V4L2_STD_NTSC,  1, 1, 1, 1, TUNER_TEMIC_4039FR5_NTSC,   -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (NTSC) FM"},
	{0x2304, 0x0214, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle Studio PCTV USB (PAL) FM"},
	{0x2304, 0x0300, -1, CODEC_SAA7113, 2, V4L2_STD_NTSC,  1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Pinnacle Studio Linx Video input cable (NTSC)"},
	{0x2304, 0x0301, -1, CODEC_SAA7113, 2, V4L2_STD_PAL,   1, 0, 1, 0, 0,                          -1, -1,  0,  3,  7, "Pinnacle Studio Linx Video input cable (PAL)"}, 
	{0x2304, 0x0419, -1, CODEC_SAA7113, 3, V4L2_STD_PAL,   1, 1, 1, 1, TUNER_TEMIC_4009FR5_PAL,    -1, -1,  0,  3,  7, "Pinnacle PCTV Bungee USB (PAL) FM"},
	{0x2400, 0x4200, -1, CODEC_SAA7111, 3, VIDEO_MODE_NTSC,  1, 0, 1, 1, TUNER_PHILIPS_NTSC_M,       -1, -1, -1, -1, -1, "Hauppauge WinTv-USB"},
	{}  /* Terminating entry */
};

/* Supported Devices */

struct usb_device_id usbvision_table [] = {
	{ USB_DEVICE(0xFFF0, 0xFFF0) },  /* Custom Dummy USBVision Device */
	{ USB_DEVICE(0x0A6F, 0x0400) },  /* Xanboo */ 
	{ USB_DEVICE(0x050d, 0x0208) },  /* Belkin USBView II */
	{ USB_DEVICE(0x0571, 0x0002) },  /* echoFX InterView Lite */
	{ USB_DEVICE(0x0573, 0x0003) },  /* USBGear USBG-V1 */
	{ USB_DEVICE(0x0573, 0x0400) },  /* D-Link V100 */
	{ USB_DEVICE(0x0573, 0x2000) },  /* X10 USB Camera */
	{ USB_DEVICE(0x0573, 0x2d00) },  /* Osprey 50 */ 
	{ USB_DEVICE(0x0573, 0x2d01) },  /* Hauppauge USB-Live Model 600 */
	{ USB_DEVICE(0x0573, 0x2101) },  /* Zoran Co. PMD (Nogatech) AV-grabber Manhattan */
	{ USB_DEVICE(0x0573, 0x4100) },  /* Nogatech USB-TV FM (NTSC) */
	{ USB_DEVICE(0x0573, 0x4110) },  /* PNY USB-TV (NTSC) FM */
	{ USB_DEVICE(0x0573, 0x4450) },  /* PixelView PlayTv-USB PRO (PAL) FM */
	{ USB_DEVICE(0x0573, 0x4550) },  /* ZTV ZT-721 2.4GHz USB A/V Receiver */
	{ USB_DEVICE(0x0573, 0x4d00) },  /* Hauppauge WinTv-USB USA */
	{ USB_DEVICE(0x0573, 0x4d01) },  /* Hauppauge WinTv-USB */
	{ USB_DEVICE(0x0573, 0x4d02) },  /* Hauppauge WinTv-USB UK */
	{ USB_DEVICE(0x0573, 0x4d03) },  /* Hauppauge WinTv-USB France */
	{ USB_DEVICE(0x0573, 0x4d10) },  /* Hauppauge WinTv-USB with FM USA radio */
	{ USB_DEVICE(0x0573, 0x4d11) },  /* Hauppauge WinTv-USB (PAL) with FM radio */
	{ USB_DEVICE(0x0573, 0x4d12) },  /* Hauppauge WinTv-USB UK with FM Radio */
	{ USB_DEVICE(0x0573, 0x4d2a) },  /* Hauppague WinTv USB Model 602 40201 Rev B285 */ 
	{ USB_DEVICE(0x0573, 0x4d2b) },  /* Hauppague WinTv USB Model 602 40201 Rev B282 */
	{ USB_DEVICE(0x0573, 0x4d2c) },  /* Hauppague WinTv USB Model 40209 Rev. E1A5 PAL*/ 
	{ USB_DEVICE(0x0573, 0x4d20) },  /* Hauppauge WinTv-USB II (PAL) FM Model 40201 Rev B226 */
	{ USB_DEVICE(0x0573, 0x4d21) },  /* Hauppauge WinTv-USB II (PAL) with FM radio*/
	{ USB_DEVICE(0x0573, 0x4d22) },  /* Hauppauge WinTv-USB II (PAL) Model 566 */
	{ USB_DEVICE(0x0573, 0x4d23) },  /* Hauppauge WinTv-USB France 4D23*/
	{ USB_DEVICE(0x0573, 0x4d25) },  /* Hauppauge WinTv-USB Model 40209 rev B234 */
	{ USB_DEVICE(0x0573, 0x4d26) },  /* Hauppauge WinTv-USB Model 40209 Rev B243 */
	{ USB_DEVICE(0x0573, 0x4d27) },  /* Hauppauge WinTv-USB Model 40204 Rev B281 */
	{ USB_DEVICE(0x0573, 0x4d28) },  /* Hauppauge WinTv-USB Model 40204 Rev B283 */
	{ USB_DEVICE(0x0573, 0x4d29) },  /* Hauppauge WinTv-USB Model 40205 Rev B298 */ 
	{ USB_DEVICE(0x0573, 0x4d30) },  /* Hauppauge WinTv-USB FM Model 40211 Rev B123 */
	{ USB_DEVICE(0x0573, 0x4d31) },  /* Hauppauge WinTv-USB III (PAL) with FM radio Model 568 */
	{ USB_DEVICE(0x0573, 0x4d32) },  /* Hauppauge WinTv-USB III (PAL) FM Model 573 */
	{ USB_DEVICE(0x0573, 0x4d35) },  /* Hauppauge WinTv-USB III (SECAM) FM Model 40219 Rev B252 */
	{ USB_DEVICE(0x0573, 0x4d37) },  /* Hauppauge WinTv-USB Model 40219 Rev E189 */
	{ USB_DEVICE(0x0768, 0x0006) },  /* Camtel Technology USB TV Genie Pro FM Model TVB330 */
	{ USB_DEVICE(0x07d0, 0x0001) },  /* Digital Video Creator I */
	{ USB_DEVICE(0x07d0, 0x0002) },  /* Global Village GV-007 (NTSC) */
	{ USB_DEVICE(0x07d0, 0x0003) },  /* Dazzle Fusion Model DVC-50 Rev 1 (NTSC) */
	{ USB_DEVICE(0x07d0, 0x0004) },  /* Dazzle Fusion Model DVC-80 Rev 1 (PAL) */
	{ USB_DEVICE(0x07d0, 0x0005) },  /* Dazzle Fusion Model DVC-90 Rev 1 (SECAM) */ 
	{ USB_DEVICE(0x2304, 0x010d) },  /* Pinnacle Studio PCTV USB (PAL) */
	{ USB_DEVICE(0x2304, 0x0109) },  /* Pinnacle Studio PCTV USB (SECAM) */
	{ USB_DEVICE(0x2304, 0x0110) },  /* Pinnacle Studio PCTV USB (PAL) */
	{ USB_DEVICE(0x2304, 0x0111) },  /* Miro PCTV USB */
	{ USB_DEVICE(0x2304, 0x0112) },  /* Pinnacle Studio PCTV USB (NTSC) with FM radio */
	{ USB_DEVICE(0x2304, 0x0210) },  /* Pinnacle Studio PCTV USB (PAL) with FM radio */
	{ USB_DEVICE(0x2304, 0x0212) },  /* Pinnacle Studio PCTV USB (NTSC) with FM radio */
	{ USB_DEVICE(0x2304, 0x0214) },  /* Pinnacle Studio PCTV USB (PAL) with FM radio */
	{ USB_DEVICE(0x2304, 0x0300) },  /* Pinnacle Studio Linx Video input cable (NTSC) */
	{ USB_DEVICE(0x2304, 0x0301) },  /* Pinnacle Studio Linx Video input cable (PAL) */ 
	{ USB_DEVICE(0x2304, 0x0419) },  /* Pinnacle PCTV Bungee USB (PAL) FM */

	{ USB_DEVICE(0x2400, 0x4200) },  /* Hauppauge WinTv-USB2 Model 42012 */

	{ }  /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usbvision_table);
