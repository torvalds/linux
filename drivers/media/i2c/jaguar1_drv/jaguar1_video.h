/* SPDX-License-Identifier: GPL-2.0 */
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: video_input.c
 *  Description	:
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _JAGUAR1_VIDEO_
#define _JAGUAR1_VIDEO_

#include "jaguar1_common.h"


/* ===============================================
 * APP -> DRV
 * =============================================== */
typedef struct _video_input_init{
	unsigned char ch;
	unsigned char format;
	unsigned char dist;
	unsigned char input;
	unsigned char val;
	unsigned char interface;
}video_input_init;

typedef struct _video_init_all{
	video_input_init ch_param[4];
}video_init_all;

typedef struct _video_output_init{
	unsigned char format;
	unsigned char port;
	unsigned char out_ch;
	unsigned char interface;
}video_output_init;

typedef struct _video_video_loss_s{
	unsigned char devnum;
	unsigned char videoloss;
	unsigned char reserve2;
} video_video_loss_s;

extern unsigned int acp_mode_enable;

void vd_jaguar1_init_set( void *p_param);
void vd_jaguar1_vo_ch_seq_set( void *p_param);
void vd_jaguar1_eq_set( void *p_param );
void vd_jaguar1_sw_reset( void *p_param );
void vd_jaguar1_get_novideo( video_video_loss_s *vidloss );

void current_bank_set( unsigned char bank );
unsigned char current_bank_get( void );
void vd_register_set( int dev, unsigned char bank, unsigned char addr, unsigned char val, int pos, int size );
void reg_val_print_flag_set( int set );

void vd_vo_seq_set( unsigned char dev, unsigned char ch, void *p_param );
#endif
/********************************************************************
 *  End of file
 ********************************************************************/
