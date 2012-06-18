/*
 * drivers/media/video/sun4i_csi/readme.txt
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

===========================================

Version: V1_11

Author:  raymonxiu

Date:     2012-1-19 19:25:36

Description:

newest module list:(X = 0 or 1)
insmod sun4i_csiX.ko ccm="ov7670" i2c_addr=0x42
insmod sun4i_csiX.ko ccm="gc0308" i2c_addr=0x42
insmod sun4i_csiX.ko ccm="gt2005" i2c_addr=0x78
insmod sun4i_csiX.ko ccm="hi704"  i2c_addr=0x60
insmod sun4i_csiX.ko ccm="sp0838" i2c_addr=0x30
insmod sun4i_csiX.ko ccm="mt9m112" i2c_addr=0xba
insmod sun4i_csiX.ko ccm="mt9m113" i2c_addr=0x78
insmod sun4i_csiX.ko ccm="ov2655" i2c_addr=0x60
insmod sun4i_csiX.ko ccm="hi253" i2c_addr=0x40
insmod sun4i_csiX.ko ccm="gc0307" i2c_addr=0x42
insmod sun4i_csiX.ko ccm="mt9d112" i2c_addr=0x78
insmod sun4i_csiX.ko ccm="ov5640" i2c_addr=0x78

V1_11
CSI: Mainly fix bugs on mt9m112,ov5640 and ov7670
1) Fix bug on calling poll or read before streamon
2) Fix bug on mt9m112 and ov5640 multiplex use
3) Fix ov7670 sensor init
4) Modify the delay on ov2655 after every i2c command
5) Modify camera debug info

V1_10
CSI: Fix bugs, add new modules support and modity power/standby interface
1) Fix bugs for CTS test
2) Fix bugs for crash when insmod after rmmod
3) Add default format for csi driver
4) Modify the power on/off,stanby on/off interface
5) Fix bugs for multipex dual sensors using one csi
6) Add gc0307, mt9d112 and ov5640 modules support
7) Fix gc0308 AWB alternation bug

V1_02
CSI: Change clock source to video pll 1 and add code for C version IC

V1_01
CSI: Add HI253 support IC version detection and fix HI704 i2c bug

V1_00
CSI:Initial version for linux 3.0.8
1) Ported from linux2.3.36


