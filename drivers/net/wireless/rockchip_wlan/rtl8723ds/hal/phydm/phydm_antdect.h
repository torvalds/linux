/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#ifndef	__PHYDMANTDECT_H__
#define    __PHYDMANTDECT_H__

#define ANTDECT_VERSION	"2.1"	/*2015.07.29 by YuChen*/

#if (defined(CONFIG_ANT_DETECTION))
/* #if( DM_ODM_SUPPORT_TYPE & (ODM_WIN |ODM_CE)) */
/* ANT Test */
#define		ANTTESTALL		0x00	/*ant A or B will be Testing*/
#define		ANTTESTA		0x01	/*ant A will be Testing*/
#define		ANTTESTB		0x02	/*ant B will be testing*/

#define	MAX_ANTENNA_DETECTION_CNT	10


struct _ANT_DETECTED_INFO {
	boolean			is_ant_detected;
	u32			db_for_ant_a;
	u32			db_for_ant_b;
	u32			db_for_ant_o;
};


enum dm_swas_e {
	antenna_a = 1,
	antenna_b = 2,
	antenna_max = 3,
};



/* 1 [1. Single Tone method] =================================================== */



void
odm_single_dual_antenna_default_setting(
	void		*p_dm_void
);

boolean
odm_single_dual_antenna_detection(
	void		*p_dm_void,
	u8			mode
);

/* 1 [2. Scan AP RSSI method] ================================================== */

#define sw_ant_div_check_before_link	odm_sw_ant_div_check_before_link

boolean
odm_sw_ant_div_check_before_link(
	void		*p_dm_void
);




/* 1 [3. PSD method] ========================================================== */


void
odm_single_dual_antenna_detection_psd(
	void		*p_dm_void
);

#endif

void
odm_sw_ant_detect_init(
	void		*p_dm_void
);


#endif
