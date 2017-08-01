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
 *****************************************************************************/
#ifndef	__ODMNOISEMONITOR_H__
#define __ODMNOISEMONITOR_H__

#define	ODM_MAX_CHANNEL_NUM					38//14+24
struct noise_level
{
	//u1Byte				value_a, value_b;
	u1Byte				value[MAX_RF_PATH];
	//s1Byte				sval_a, sval_b;
	s1Byte				sval[MAX_RF_PATH];
	
	//s4Byte				noise_a=0, noise_b=0,sum_a=0, sum_b=0;
	//s4Byte				noise[ODM_RF_PATH_MAX];
	s4Byte				sum[MAX_RF_PATH];
	//u1Byte				valid_cnt_a=0, valid_cnt_b=0, 
	u1Byte				valid[MAX_RF_PATH];
	u1Byte				valid_cnt[MAX_RF_PATH];

};


typedef struct _ODM_NOISE_MONITOR_
{
	s1Byte			noise[MAX_RF_PATH];
	s2Byte			noise_all;	
}ODM_NOISE_MONITOR;

s2Byte ODM_InbandNoise_Monitor(PVOID pDM_VOID,u8 bPauseDIG,u8 IGIValue,u32 max_time);

#endif
