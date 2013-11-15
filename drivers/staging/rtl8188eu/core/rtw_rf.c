/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_RF_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>


struct ch_freq {
	u32 channel;
	u32 frequency;
};

static struct ch_freq ch_freq_map[] = {
	{1, 2412}, {2, 2417}, {3, 2422}, {4, 2427}, {5, 2432},
	{6, 2437}, {7, 2442}, {8, 2447}, {9, 2452}, {10, 2457},
	{11, 2462}, {12, 2467}, {13, 2472}, {14, 2484},
	/*  UNII */
	{36, 5180}, {40, 5200}, {44, 5220}, {48, 5240}, {52, 5260},
	{56, 5280}, {60, 5300}, {64, 5320}, {149, 5745}, {153, 5765},
	{157, 5785}, {161, 5805}, {165, 5825}, {167, 5835}, {169, 5845},
	{171, 5855}, {173, 5865},
	/* HiperLAN2 */
	{100, 5500}, {104, 5520}, {108, 5540}, {112, 5560}, {116, 5580},
	{120, 5600}, {124, 5620}, {128, 5640}, {132, 5660}, {136, 5680},
	{140, 5700},
	/* Japan MMAC */
	{34, 5170}, {38, 5190}, {42, 5210}, {46, 5230},
	/*  Japan */
	{184, 4920}, {188, 4940}, {192, 4960}, {196, 4980},
	{208, 5040},/* Japan, means J08 */
	{212, 5060},/* Japan, means J12 */
	{216, 5080},/* Japan, means J16 */
};

static int ch_freq_map_num = (sizeof(ch_freq_map) / sizeof(struct ch_freq));

u32 rtw_ch2freq(u32 channel)
{
	u8	i;
	u32	freq = 0;

	for (i = 0; i < ch_freq_map_num; i++) {
		if (channel == ch_freq_map[i].channel) {
			freq = ch_freq_map[i].frequency;
				break;
		}
	}
	if (i == ch_freq_map_num)
		freq = 2412;

	return freq;
}

u32 rtw_freq2ch(u32 freq)
{
	u8	i;
	u32	ch = 0;

	for (i = 0; i < ch_freq_map_num; i++) {
		if (freq == ch_freq_map[i].frequency) {
			ch = ch_freq_map[i].channel;
				break;
		}
	}
	if (i == ch_freq_map_num)
		ch = 1;

	return ch;
}
