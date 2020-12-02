// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2018 	NEXTCHIP Inc. All rights reserved.
 *  Module		: nvp6168_eq_table.c
 *  Description	:
 *  Author		:
 *  Date         :
 *  Version		: Version 0.1
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#ifndef _NVP6168_EQ_TABLE_H_
#define _NVP6168_EQ_TABLE_H_

static nvp6158_video_equalizer_distance_table_s nvp6168_equalizer_distance_fmtdef[ NC_VIVO_CH_FORMATDEF_MAX ] =
{
	[ CVI_4M_30P ] = {
		{
			.hsync_stage[0] = 0x8ECE24,
			.hsync_stage[1] = 0x8B9A7D,
			.hsync_stage[2] = 0x874271,
			.hsync_stage[3] = 0x828FA9,
			.hsync_stage[4] = 0x80CC69,
			.hsync_stage[5] = 0x7E35AA,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ CVI_4M_25P ] = {
		{
			.hsync_stage[0] = 0x91616A,
			.hsync_stage[1] = 0x8D3C8E,
			.hsync_stage[2] = 0x88D10A,
			.hsync_stage[3] = 0x843841,
			.hsync_stage[4] = 0x800696,
			.hsync_stage[5] = 0x7D2AC6,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,

		},
	},
	[ CVI_5M_20P ] = {
		{
			.hsync_stage[0] = 0x34EDD3,
			.hsync_stage[1] = 0x3107C1,
			.hsync_stage[2] = 0x2DBC0D,
			.hsync_stage[3] = 0x2B6A06,
			.hsync_stage[4] = 0x29946A,
			.hsync_stage[5] = 0x2831D9,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,

		},
	},

	[ CVI_8M_15P ] = {
		{
			.hsync_stage[0] = 0x919833,
			.hsync_stage[1] = 0x8D8CD8,
			.hsync_stage[2] = 0x8874E5,
			.hsync_stage[3] = 0x85C829,
			.hsync_stage[4] = 0x822097,
			.hsync_stage[5] = 0x7FF3B7,
		},
		{
			.agc_stage[0]   = 0x0C,
			.agc_stage[1]   = 0x26,
			.agc_stage[2]   = 0xFF,
			.agc_stage[3]   = 0xFF,
			.agc_stage[4]   = 0xFF,
			.agc_stage[5]   = 0xFF,
		},
	},
	[ CVI_8M_12_5P ] = {
		{
			.hsync_stage[0] = 0x8EEEBA,
			.hsync_stage[1] = 0x8A8443,
			.hsync_stage[2] = 0x8556D0,
			.hsync_stage[3] = 0x83CAF0,
			.hsync_stage[4] = 0x80ECCC,
			.hsync_stage[5] = 0x7DC72D,
		},
		{
			.agc_stage[0]   = 0x0C,
			.agc_stage[1]   = 0x26,
			.agc_stage[2]   = 0xFF,
			.agc_stage[3]   = 0xFF,
			.agc_stage[4]   = 0xFF,
			.agc_stage[5]   = 0xFF,
		},
	},
	[ CVI_FHD_30P ] = {
		{
			.hsync_stage[0] = 0x8E26A8,
			.hsync_stage[1] = 0x8ADEC6,
			.hsync_stage[2] = 0x877648,
			.hsync_stage[3] = 0x827821,
			.hsync_stage[4] = 0x7F4122,
			.hsync_stage[5] = 0x7CBF4C,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},

	},
	[ CVI_FHD_25P ] = {
		{
			.hsync_stage[0] = 0x8FADED,
			.hsync_stage[1] = 0x8B0FA4,
			.hsync_stage[2] = 0x88E4C2,
			.hsync_stage[3] = 0x837985,
			.hsync_stage[4] = 0x7F0CC4,
			.hsync_stage[5] = 0x7B6A7E,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ CVI_HD_30P ] = {
		{
			.hsync_stage[0] = 0x8C135D,
			.hsync_stage[1] = 0x884F58,
			.hsync_stage[2] = 0x83CC33,
			.hsync_stage[3] = 0x8107AD,
			.hsync_stage[4] = 0x7ACF88,
			.hsync_stage[5] = 0x77D736,
			.hsync_stage[6] = 0x770B9A, /* 600m  */
			.hsync_stage[7] = 0x734573, /* 700m  */
			.hsync_stage[8] = 0x38DA6A, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ CVI_HD_25P ] = {
		{
			.hsync_stage[0] = 0x8BA573,
			.hsync_stage[1] = 0x8860AE,
			.hsync_stage[2] = 0x852324,
			.hsync_stage[3] = 0x811FB1,
			.hsync_stage[4] = 0x7CCAC8,
			.hsync_stage[5] = 0x770BD1,
			.hsync_stage[6] = 0x75B991, /* 600m  */
			.hsync_stage[7] = 0x71D270, /* 700m  */
			.hsync_stage[8] = 0x385CFF, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ CVI_HD_50P ] = {
		{
			.hsync_stage[0] = 0x90215B,
			.hsync_stage[1] = 0x8CB0AA,
			.hsync_stage[2] = 0x87EC3B,
			.hsync_stage[3] = 0x85D89F,
			.hsync_stage[4] = 0x82449F,
			.hsync_stage[5] = 0x7F59F9,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ CVI_HD_60P ] = {
		{
			.hsync_stage[0] = 0x90E55C,
			.hsync_stage[1] = 0x8CD508,
			.hsync_stage[2] = 0x896950,
			.hsync_stage[3] = 0x868B76,
			.hsync_stage[4] = 0x82D331,
			.hsync_stage[5] = 0x809418,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ AHD30_4M_25P ] = {
		{
			.hsync_stage[0] = 0x9345D8,
			.hsync_stage[1] = 0x8F586A,
			.hsync_stage[2] = 0x8A92F4,
			.hsync_stage[3] = 0x871984,
			.hsync_stage[4] = 0x83FD7E,
			.hsync_stage[5] = 0x823CFC,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_4M_30P ] = {
		{
			.hsync_stage[0] = 0x91CF31,
			.hsync_stage[1] = 0x8C8043,
			.hsync_stage[2] = 0x8901BC,
			.hsync_stage[3] = 0x858916,
			.hsync_stage[4] = 0x82CC39,
			.hsync_stage[5] = 0x7FE272,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_4M_15P ] = {
		{
			.hsync_stage[0] = 0x8E6FD2,
			.hsync_stage[1] = 0x8BB76E,
			.hsync_stage[2] = 0x87F473,
			.hsync_stage[3] = 0x84D01F,
			.hsync_stage[4] = 0x823C59,
			.hsync_stage[5] = 0x7FB8AF,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_8M_7_5P ] = {
		{
			.hsync_stage[0] = 0x8EE2AC,
			.hsync_stage[1] = 0x8BB641,
			.hsync_stage[2] = 0x868E47,
			.hsync_stage[3] = 0x83F341,
			.hsync_stage[4] = 0x7FB13A,
			.hsync_stage[5] = 0x7D8B13,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_8M_15P ] = {
		{
			.hsync_stage[0] = 0x3799AE,
			.hsync_stage[1] = 0x3444B5,
			.hsync_stage[2] = 0x30D56F,
			.hsync_stage[3] = 0x2E1A08,
			.hsync_stage[4] = 0x2BE85F,
			.hsync_stage[5] = 0x2A2AA3,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_8M_12_5P ] = {
		{
			.hsync_stage[0] = 0x93534C,
			.hsync_stage[1] = 0x8F4F50,
			.hsync_stage[2] = 0x8B3DF0,
			.hsync_stage[3] = 0x87AA86,
			.hsync_stage[4] = 0x84E69C,
			.hsync_stage[5] = 0x829302,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ TVI_4M_15P ] = {
		{
			.hsync_stage[0] = 0x36D233,
			.hsync_stage[1] = 0x34188C,
			.hsync_stage[2] = 0x31059E,
			.hsync_stage[3] = 0x2E6C01,
			.hsync_stage[4] = 0x2C188B,
			.hsync_stage[5] = 0x2A870F,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ TVI_4M_15P ] = {
		{
			.hsync_stage[0] = 0x35DE4D,
			.hsync_stage[1] = 0x331214,
			.hsync_stage[2] = 0x2FDCA5,
			.hsync_stage[3] = 0x2DA838,
			.hsync_stage[4] = 0x2BEAFD,
			.hsync_stage[5] = 0x2A702F,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ TVI_4M_25P ] = {
		{
			.hsync_stage[0] = 0x35DE4D,
			.hsync_stage[1] = 0x331214,
			.hsync_stage[2] = 0x2FDCA5,
			.hsync_stage[3] = 0x2DA838,
			.hsync_stage[4] = 0x2BEAFD,
			.hsync_stage[5] = 0x2A702F,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ TVI_4M_30P ] = {
		{
			.hsync_stage[0] = 0x35CF85,
			.hsync_stage[1] = 0x32B06F,
			.hsync_stage[2] = 0x2F8C60,
			.hsync_stage[3] = 0x2D2ACD,
			.hsync_stage[4] = 0x2B7B57,
			.hsync_stage[5] = 0x29F608,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_5M_20P ] = {
		{
			.hsync_stage[0] = 0x9357E8,
			.hsync_stage[1] = 0x8F2A88,
			.hsync_stage[2] = 0x8B88DE,
			.hsync_stage[3] = 0x8845A7,
			.hsync_stage[4] = 0x8501D2,
			.hsync_stage[5] = 0x82ED6D,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},
	[ AHD30_5_3M_20P ] = {
		{
			.hsync_stage[0] = 0x92DEB4,
			.hsync_stage[1] = 0x8F64D1,
			.hsync_stage[2] = 0x8B4B6B,
			.hsync_stage[3] = 0x87590B,
			.hsync_stage[4] = 0x844EBD,
			.hsync_stage[5] = 0x81ADC9,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},

	[ TVI_5M_12_5P ] = {
		{
			.hsync_stage[0] = 0x35BDFD,
			.hsync_stage[1] = 0x32EA38,
			.hsync_stage[2] = 0x2FC1E7,
			.hsync_stage[3] = 0x2D1A7E,
			.hsync_stage[4] = 0x2B3453,
			.hsync_stage[5] = 0x29D1E2,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ TVI_5M_20P ] = {
		{
			.hsync_stage[0] = 0x37958B,
			.hsync_stage[1] = 0x347FCB,
			.hsync_stage[2] = 0x31012D,
			.hsync_stage[3] = 0x2EF8E1,
			.hsync_stage[4] = 0x2C8045,
			.hsync_stage[5] = 0x2B972D,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ TVI_8M_15P ] = {
		{
			.hsync_stage[0] = 0x37568B,
			.hsync_stage[1] = 0x33EE81,
			.hsync_stage[2] = 0x303004,
			.hsync_stage[3] = 0x2DB8ED,
			.hsync_stage[4] = 0x2B7D96,
			.hsync_stage[5] = 0x2A3514,
		},
		{
			.agc_stage[0]	= 0x0C,
			.agc_stage[1]	= 0x26,
			.agc_stage[2]	= 0xFF,
			.agc_stage[3]	= 0xFF,
			.agc_stage[4]	= 0xFF,
			.agc_stage[5]	= 0xFF,
		},
	},
	[ TVI_8M_12_5P ] = {
		{
			.hsync_stage[0] = 0x376F04,
			.hsync_stage[1] = 0x33E296,
			.hsync_stage[2] = 0x30A70C,
			.hsync_stage[3] = 0x2EB24B,
			.hsync_stage[4] = 0x2CED34,
			.hsync_stage[5] = 0x2B7888,
		},
		{
			.agc_stage[0]	= 0x0C,
			.agc_stage[1]	= 0x26,
			.agc_stage[2]	= 0xFF,
			.agc_stage[3]	= 0xFF,
			.agc_stage[4]	= 0xFF,
			.agc_stage[5]	= 0xFF,
		},
	},
	[ TVI_3M_18P ] = {
		{
			.hsync_stage[0] = 0x359BAE,
			.hsync_stage[1] = 0x332C43,
			.hsync_stage[2] = 0x3005D7,
			.hsync_stage[3] = 0x2DB71C,
			.hsync_stage[4] = 0x2BC581,
			.hsync_stage[5] = 0x2A24E6,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ AHD20_1080P_25P ] = { /* o */
		{
			.hsync_stage[0] = 0x8E63AE,
			.hsync_stage[1] = 0x8B90EE,
			.hsync_stage[2] = 0x8849BD,
			.hsync_stage[3] = 0x84D310,
			.hsync_stage[4] = 0x826C8C,
			.hsync_stage[5] = 0x8063F2,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ AHD20_1080P_30P ] = { /* o */
		{
			.hsync_stage[0] = 0x8B3D4D,
			.hsync_stage[1] = 0x8A1919,
			.hsync_stage[2] = 0x84AB53,
			.hsync_stage[3] = 0x807C8A,
			.hsync_stage[4] = 0x7DF22E,
			.hsync_stage[5] = 0x7A7F57,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ AHD20_1080P_12_5P_EX ] = { /* o */
			{
				.hsync_stage[0] = 0x8F06D5,	/* short */
				.hsync_stage[1] = 0x8C3CA0,	/* 100m  */
				.hsync_stage[2] = 0x896997,	/* 200m  */
				.hsync_stage[3] = 0x859D32,	/* 300m  */
				.hsync_stage[4] = 0x821cdc,	/* 400m  */
				.hsync_stage[5] = 0x7EB58A,	/* 500m  */
			},
			{
				.agc_stage[0]   = 0x00,
				.agc_stage[1]   = 0x00,
				.agc_stage[2]   = 0x00,
				.agc_stage[3]   = 0x00,
				.agc_stage[4]   = 0x00,
				.agc_stage[5]   = 0x00,
			},
		},
	[ AHD20_1080P_15P_EX ] = { /* o */
			{
				#if 0
				.hsync_stage[0] = 0x8F0A3E,	/* short */
				.hsync_stage[1] = 0x8BE9B4,	/* 100m  */
				.hsync_stage[2] = 0x8924F4,	/* 200m  */
				.hsync_stage[3] = 0x84E8EB,	/* 300m  */
				.hsync_stage[4] = 0x8108d5,	/* 400m  */
				.hsync_stage[5] = 0x7BE0E9,	/* 500m  */
				#else
				.hsync_stage[0] = 0x830A3E,	/* short */
				.hsync_stage[1] = 0x79E9B4,	/* 100m  */
				.hsync_stage[2] = 0x7624F4,	/* 200m  */
				.hsync_stage[3] = 0x71E8EB,	/* 300m  */
				.hsync_stage[4] = 0x6d08d5,	/* 400m  */
				.hsync_stage[5] = 0x67E0E9,	/* 500m  */
				#endif
			},
			{
				.agc_stage[0]   = 0x00,
				.agc_stage[1]   = 0x00,
				.agc_stage[2]   = 0x00,
				.agc_stage[3]   = 0x00,
				.agc_stage[4]   = 0x00,
				.agc_stage[5]   = 0x00,
			},
		},
	[ AHD20_960P_25P ] = { /* o */
		{
			.hsync_stage[0] = 0x8E63AE,
			.hsync_stage[1] = 0x8B90EE,
			.hsync_stage[2] = 0x8849BD,
			.hsync_stage[3] = 0x84D310,
			.hsync_stage[4] = 0x826C8C,
			.hsync_stage[5] = 0x8063F2,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ AHD20_960P_30P ] = { /* o */
		{
			.hsync_stage[0] = 0x8B3D4D,
			.hsync_stage[1] = 0x8A1919,
			.hsync_stage[2] = 0x84AB53,
			.hsync_stage[3] = 0x807C8A,
			.hsync_stage[4] = 0x7DF22E,
			.hsync_stage[5] = 0x7A7F57,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ TVI_FHD_30P ] = {
		{
			.hsync_stage[0] = 0x359F01,
			.hsync_stage[1] = 0x32784D,
			.hsync_stage[2] = 0x2F6A51,
			.hsync_stage[3] = 0x2CDB9F,
			.hsync_stage[4] = 0x2AE0D4,
			.hsync_stage[5] = 0x297011,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ TVI_FHD_25P ] = {
		{
			.hsync_stage[0] = 0x35C41B,
			.hsync_stage[1] = 0x33116E,
			.hsync_stage[2] = 0x301917,
			.hsync_stage[3] = 0x2DA0DF,
			.hsync_stage[4] = 0x2B7660,
			.hsync_stage[5] = 0x29D264,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},
	[ AHD20_720P_25P_EX_Btype ] = { /* o */
		{
			.hsync_stage[0] = 0x8E6C4A,
			.hsync_stage[1] = 0x8B1DB9,
			.hsync_stage[2] = 0x86BC00,
			.hsync_stage[3] = 0x8348E0,
			.hsync_stage[4] = 0x7F7C83,
			.hsync_stage[5] = 0x7D7E4C,
			.hsync_stage[6] = 0x7B6922, /* 600m  */
			.hsync_stage[7] = 0x7906E2, /* 700m  */
			.hsync_stage[8] = 0x3C30A5, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ AHD20_720P_30P_EX_Btype ] = { /* o */
		{
			.hsync_stage[0] = 0x8BD1CB,
			.hsync_stage[1] = 0x8822D9,
			.hsync_stage[2] = 0x845130,
			.hsync_stage[3] = 0x80C56E,
			.hsync_stage[4] = 0x7B1554,
			.hsync_stage[5] = 0x78945E,
			.hsync_stage[6] = 0x76B80E, /* 600m  */
			.hsync_stage[7] = 0x73CB89, /* 700m  */
			.hsync_stage[8] = 0x3955E6, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ AHD20_720P_25P ] = { /* o */
		{
			.hsync_stage[0] = 0x8E6C4A,
			.hsync_stage[1] = 0x8B1DB9,
			.hsync_stage[2] = 0x86BC00,
			.hsync_stage[3] = 0x8348E0,
			.hsync_stage[4] = 0x7F7C83,
			.hsync_stage[5] = 0x7D7E4C,
			.hsync_stage[6] = 0x7B6922, /* 600m  */
			.hsync_stage[7] = 0x7906E2, /* 700m  */
			.hsync_stage[8] = 0x3C30A5, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},
	[ AHD20_720P_30P ] = { /* o */
		{
			.hsync_stage[0] = 0x8BD1CB,
			.hsync_stage[1] = 0x8822D9,
			.hsync_stage[2] = 0x845130,
			.hsync_stage[3] = 0x80C56E,
			.hsync_stage[4] = 0x7B1554,
			.hsync_stage[5] = 0x78945E,
			.hsync_stage[6] = 0x76B80E, /* 600m  */
			.hsync_stage[7] = 0x73CB89, /* 700m  */
			.hsync_stage[8] = 0x3955E6, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},

	[ CVI_HD_30P_EX ] = {
		{
			.hsync_stage[0] = 0x8C135D,
			.hsync_stage[1] = 0x884F58,
			.hsync_stage[2] = 0x83CC33,
			.hsync_stage[3] = 0x8107AD,
			.hsync_stage[4] = 0x7ACF88,
			.hsync_stage[5] = 0x77D736,
			.hsync_stage[6] = 0x770B9A, /* 600m  */
			.hsync_stage[7] = 0x734573, /* 700m  */
			.hsync_stage[8] = 0x38DA6A, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},

	[ CVI_HD_25P_EX ] = {
		{
			.hsync_stage[0] = 0x8BA573,
			.hsync_stage[1] = 0x8860AE,
			.hsync_stage[2] = 0x852324,
			.hsync_stage[3] = 0x811FB1,
			.hsync_stage[4] = 0x7CCAC8,
			.hsync_stage[5] = 0x770BD1,
			.hsync_stage[6] = 0x75B991, /* 600m  */
			.hsync_stage[7] = 0x71D270, /* 700m  */
			.hsync_stage[8] = 0x385CFF, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},

	[ TVI_HD_30P_EX ] = {
		{
			.hsync_stage[0] = 0x360C87,
			.hsync_stage[1] = 0x32F15A,
			.hsync_stage[2] = 0x2FDACA,
			.hsync_stage[3] = 0x2D5591,
			.hsync_stage[4] = 0x2B91DE,
			.hsync_stage[5] = 0x2A1F7F,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},

	[ TVI_HD_25P_EX ] = {
		{
			.hsync_stage[0] = 0x35F1DC,
			.hsync_stage[1] = 0x330C82,
			.hsync_stage[2] = 0x2FE3B2,
			.hsync_stage[3] = 0x2D7E56,
			.hsync_stage[4] = 0x2B96C2,
			.hsync_stage[5] = 0x29F7F4,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},

	[ TVI_HD_B_30P_EX ] = {
		{
			.hsync_stage[0] = 0x3259F8,
			.hsync_stage[1] = 0x2F45CF,
			.hsync_stage[2] = 0x2C13B5,
			.hsync_stage[3] = 0x297E56,
			.hsync_stage[4] = 0x270130,
			.hsync_stage[5] = 0x250BB5,
			.hsync_stage[6] = 0x23D592, /* 600m  */
			.hsync_stage[7] = 0x226F62, /* 700m  */
			.hsync_stage[8] = 0x10F18D, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},

	[ TVI_HD_B_25P_EX ] = {
		{
			.hsync_stage[0] = 0x31F0A9,
			.hsync_stage[1] = 0x2EF3C6,
			.hsync_stage[2] = 0x2BEC81,
			.hsync_stage[3] = 0x298722,
			.hsync_stage[4] = 0x276486,
			.hsync_stage[5] = 0x25D71F,
			.hsync_stage[6] = 0x24D534, /* 600m  */
			.hsync_stage[7] = 0x237BB2, /* 700m  */
			.hsync_stage[8] = 0x11768C, /* 800m  */
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
			.agc_stage[6]	= 0x00,
			.agc_stage[7]	= 0x00,
			.agc_stage[8]	= 0x00,
		},
	},

	[ TVI_HD_60P ] = {
		{
			.hsync_stage[0] = 0x360D63,
			.hsync_stage[1] = 0x330DAD,
			.hsync_stage[2] = 0x2F9AC6,
			.hsync_stage[3] = 0x2D703E,
			.hsync_stage[4] = 0x2BA16A,
			.hsync_stage[5] = 0x2A31E5,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},

	[ TVI_HD_50P ] = {
		{
			.hsync_stage[0] = 0x35FA7E,
			.hsync_stage[1] = 0x333FE2,
			.hsync_stage[2] = 0x3099B4,
			.hsync_stage[3] = 0x2DFAE8,
			.hsync_stage[4] = 0x2BDFC1,
			.hsync_stage[5] = 0x2A734C,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		},
	},

	[ AHD30_5M_12_5P ] = {
		{
			.hsync_stage[0] = 0x8EC141,
			.hsync_stage[1] = 0x8B3C01,
			.hsync_stage[2] = 0x86C788,
			.hsync_stage[3] = 0x83DB08,
			.hsync_stage[4] = 0x806731,
			.hsync_stage[5] = 0x7E2193,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},

	[ AHD30_3M_18P ] = {
		{
			.hsync_stage[0] = 0x8EE698,
			.hsync_stage[1] = 0x8C3285,
			.hsync_stage[2] = 0x88677E,
			.hsync_stage[3] = 0x8429A3,
			.hsync_stage[4] = 0x80DB83,
			.hsync_stage[5] = 0x7EA105,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},

	[ AHD30_3M_25P ] = {
		{
			.hsync_stage[0] = 0x92E5B1,
			.hsync_stage[1] = 0x8F2E19,
			.hsync_stage[2] = 0x8B3731,
			.hsync_stage[3] = 0x885B7B,
			.hsync_stage[4] = 0x857793,
			.hsync_stage[5] = 0x835E8F,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},

	[ AHD30_3M_30P ] = {
		{
			.hsync_stage[0] = 0x92748C,
			.hsync_stage[1] = 0x8EA168,
			.hsync_stage[2] = 0x8B1B23,
			.hsync_stage[3] = 0x87A8FE,
			.hsync_stage[4] = 0x84798B,
			.hsync_stage[5] = 0x82277E,
		},
		{
			.agc_stage[0]   = 0x00,
			.agc_stage[1]   = 0x00,
			.agc_stage[2]   = 0x00,
			.agc_stage[3]   = 0x00,
			.agc_stage[4]   = 0x00,
			.agc_stage[5]   = 0x00,
		}
	},

};

static video_equalizer_value_table_s nvp6168_equalizer_value_fmtdef[ NC_VIVO_CH_FORMATDEF_MAX ] =
{
	[ CVI_4M_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x47, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x91, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x81, .contrast[1] 		= 0x81, .contrast[2] 		= 0x81, .contrast[3] 		= 0x81, .contrast[4] 		= 0x81, .contrast[5] 		= 0x81,
			.h_peaking[0] 		= 0x20, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x20, .u_gain[1] 			= 0x20, .u_gain[2] 			= 0x20, .u_gain[3] 			= 0x20, .u_gain[4] 			= 0x20, .u_gain[5] 			= 0x20,
			.v_gain[0] 			= 0xe8, .v_gain[1] 			= 0xe8, .v_gain[2] 			= 0xe8, .v_gain[3] 			= 0xe8, .v_gain[4] 			= 0xe8, .v_gain[5] 			= 0xe8,
			.u_offset[0] 		= 0xfe, .u_offset[1] 		= 0xfe, .u_offset[2] 		= 0xfe, .u_offset[3] 		= 0xfe, .u_offset[4] 		= 0xfe, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xfa, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfa, .v_offset[3] 		= 0xfa, .v_offset[4] 		= 0xfa, .v_offset[5] 		= 0xfa,

			.black_level[0] 	= 0x81, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x81, .black_level[4] 	= 0x81, .black_level[5] 	= 0x92,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xd0, .sub_saturation[1] 	= 0xd0, .sub_saturation[2] 	= 0xd0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0x70,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x90, .c_option[2] 		= 0x90, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x82, .h_delay_a[2] = 0x81, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x79, .h_scaler2[1]   = 0x79, .h_scaler2[2]   = 0x79, .h_scaler2[3]   = 0x79, .h_scaler2[4]   = 0x79, .h_scaler2[5]   = 0x79,
			/*B9 0x98*/	.h_scaler3[0]  = 0x80, .h_scaler3[1]   = 0x80, .h_scaler3[2]   = 0x80, .h_scaler3[3]   = 0x80, .h_scaler3[4]   = 0x80, .h_scaler3[5]   = 0x80,
			/*B9 0x99*/	.h_scaler4[0]  = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x03, .h_scaler6[1]   = 0x03, .h_scaler6[2]   = 0x03, .h_scaler6[3]   = 0x03, .h_scaler6[4]   = 0x03, .h_scaler6[5]   = 0x03,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,
			/*B0 0x81*/	.format_set1[0] = 0x0f, .format_set1[1] = 0x0f, .format_set1[2] = 0x0f, .format_set1[3] = 0x0f, .format_set1[4] = 0x0f, .format_set1[5] = 0x0f,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_4M_30P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x47, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x91, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x81, .contrast[1] 		= 0x81, .contrast[2] 		= 0x81, .contrast[3] 		= 0x81, .contrast[4] 		= 0x81, .contrast[5] 		= 0x81,
			.h_peaking[0] 		= 0x20, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x20, .u_gain[1] 			= 0x20, .u_gain[2] 			= 0x20, .u_gain[3] 			= 0x20, .u_gain[4] 			= 0x20, .u_gain[5] 			= 0x20,
			.v_gain[0] 			= 0xe8, .v_gain[1] 			= 0xe8, .v_gain[2] 			= 0xe8, .v_gain[3] 			= 0xe8, .v_gain[4] 			= 0xe8, .v_gain[5] 			= 0xe8,
			.u_offset[0] 		= 0xfe, .u_offset[1] 		= 0xfe, .u_offset[2] 		= 0xfe, .u_offset[3] 		= 0xfe, .u_offset[4] 		= 0xfe, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xfa, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfa, .v_offset[3] 		= 0xfa, .v_offset[4] 		= 0xfa, .v_offset[5] 		= 0xfa,

			.black_level[0] 	= 0x81, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x81, .black_level[4] 	= 0x81, .black_level[5] 	= 0x92,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xd0, .sub_saturation[1] 	= 0xd0, .sub_saturation[2] 	= 0xd0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0x70,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x90, .c_option[2] 		= 0x90, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x73, .h_delay_a[1] = 0x78, .h_delay_a[2] = 0x7a, .h_delay_a[3] = 0x7a, .h_delay_a[4] = 0x7a, .h_delay_a[5] = 0x7a,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]  = 0x69, .h_scaler2[1]   = 0x69, .h_scaler2[2]   = 0x69, .h_scaler2[3]   = 0x69, .h_scaler2[4]   = 0x69, .h_scaler2[5]   = 0x69,
			.h_scaler3[0]  = 0x90, .h_scaler3[1]   = 0x90, .h_scaler3[2]   = 0x90, .h_scaler3[3]   = 0x90, .h_scaler3[4]   = 0x90, .h_scaler3[5]   = 0x90,
			.h_scaler4[0]  = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01,
			.h_scaler5[0]  = 0x04, .h_scaler5[1]   = 0x04, .h_scaler5[2]   = 0x04, .h_scaler5[3]   = 0x04, .h_scaler5[4]   = 0x04, .h_scaler5[5]   = 0x04,
			.h_scaler6[0]  = 0x01, .h_scaler6[1]   = 0x01, .h_scaler6[2]   = 0x01, .h_scaler6[3]   = 0x01, .h_scaler6[4]   = 0x01, .h_scaler6[5]   = 0x01,
			.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,

			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x0e, .format_set1[1] = 0x0e, .format_set1[2] = 0x0e, .format_set1[3] = 0x0e, .format_set1[4] = 0x0e, .format_set1[5] = 0x0e,
			.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_5M_20P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x37, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7A, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x88,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x10, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0xb2, .c_filter[3] 		= 0xb2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00,
			.v_gain[0] 			= 0xf0, .v_gain[1] 			= 0xf0, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0xfc, .u_offset[1] 		= 0xfc, .u_offset[2] 		= 0xfc, .u_offset[3] 		= 0xfc, .u_offset[4] 		= 0xfc, .u_offset[5] 		= 0xfc,
			.v_offset[0] 		= 0xfc, .v_offset[1] 		= 0xfc, .v_offset[2] 		= 0xfc, .v_offset[3] 		= 0xfc, .v_offset[4] 		= 0xfc, .v_offset[5] 		= 0xfc,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x80, .black_level[2] 	= 0x80, .black_level[3] 	= 0x80, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x20, .acc_ref[5]			= 0x20,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0xa0, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa0, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0x9c, .sub_saturation[4] 	= 0x60, .sub_saturation[5] 	= 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0xa0, .c_option[2] 		= 0xb0, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x60, .h_delay_a[1] = 0x60, .h_delay_a[2] = 0x60, .h_delay_a[3] = 0x60, .h_delay_a[4] = 0x42, .h_delay_a[5] = 0x60,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x0f, .h_delay_c[1] = 0x0f, .h_delay_c[2] = 0x0f, .h_delay_c[3] = 0x0f, .h_delay_c[4] = 0x0f, .h_delay_c[5] = 0x0f,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5]   = 0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0f, .format_set1[1] = 0x0f, .format_set1[2] = 0x0f, .format_set1[3] = 0x0f, .format_set1[4] = 0x0f, .format_set1[5] = 0x0f,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x27, .v_delay[1]     = 0x27, .v_delay[2]     = 0x27, .v_delay[3]     = 0x27, .v_delay[4]     = 0x27, .v_delay[5]     = 0x27,
			},
		},
	        [ CVI_8M_12_5P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x95, .deq_a_sel[4] 	= 0x94, .deq_a_sel[5] 	= 0x95,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x7e, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x7a, .contrast[3] 		= 0x79, .contrast[4] 		= 0x79, .contrast[5] 		= 0x79,
			.h_peaking[0] 		= 0x20, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x30, .h_peaking[3] 		= 0x30, .h_peaking[4] 		= 0x30, .h_peaking[5] 		= 0x30,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x91, .c_filter[2]	 	= 0x91, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x01, .hue[3] 			= 0x01, .hue[4] 			= 0x01, .hue[5] 			= 0x01,
			.u_gain[0] 			= 0xd8, .u_gain[1] 			= 0xf0, .u_gain[2] 			= 0x14, .u_gain[3] 			= 0x14, .u_gain[4] 			= 0x14, .u_gain[5] 			= 0x14,
			.v_gain[0] 			= 0xe0, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0x14, .v_gain[3] 			= 0x14, .v_gain[4] 			= 0x14, .v_gain[5] 			= 0x14,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xf6, .u_offset[3] 		= 0xf6, .u_offset[4] 		= 0xf6, .u_offset[5] 		= 0xf6,
			.v_offset[0] 		= 0xfc, .v_offset[1] 		= 0xfc, .v_offset[2] 		= 0xfc, .v_offset[3] 		= 0xfc, .v_offset[4] 		= 0xfc, .v_offset[5] 		= 0xfc,

			.black_level[0] 	= 0x85, .black_level[1] 	= 0x85, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8b, .black_level[4] 	= 0x8b, .black_level[5] 	= 0x8b,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0x90, .sub_saturation[2] 	= 0x90, .sub_saturation[3] 	= 0x80, .sub_saturation[4] 	= 0x48, .sub_saturation[5] 	= 0x20,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0xc0, .c_option[1] 		= 0xc0, .c_option[2] 		= 0xc0, .c_option[3] 		= 0xc0, .c_option[4] 		= 0xc0, .c_option[5] 		= 0xc0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]   = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]   = 0x99, .h_scaler2[1]   = 0x99, .h_scaler2[2]   = 0x99, .h_scaler2[3]   = 0x99, .h_scaler2[4]   = 0x99, .h_scaler2[5]   = 0x99,
			.h_scaler3[0]   = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			.h_scaler4[0]   = 0x02, .h_scaler4[1]   = 0x02, .h_scaler4[2]   = 0x02, .h_scaler4[3]   = 0x02, .h_scaler4[4]   = 0x02, .h_scaler4[5]   = 0x02,
			.h_scaler5[0]   = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]   = 0x02, .h_scaler6[1]   = 0x02, .h_scaler6[2]   = 0x02, .h_scaler6[3]   = 0x02, .h_scaler6[4]   = 0x02, .h_scaler6[5]   = 0x02,
			.h_scaler7[0]   = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]   = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]   = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,

			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x01, .format_set1[1] = 0x01, .format_set1[2] = 0x01, .format_set1[3] = 0x01, .format_set1[4] = 0x01, .format_set1[5] = 0x01,
			.format_set2[0] = 0x0a, .format_set2[1] = 0x0a, .format_set2[2] = 0x0a, .format_set2[3] = 0x0a, .format_set2[4] = 0x0a, .format_set2[5] = 0x0a,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_8M_15P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x95, .deq_a_sel[4] 	= 0x94, .deq_a_sel[5] 	= 0x95,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x7e, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x7a, .contrast[3] 		= 0x79, .contrast[4] 		= 0x79, .contrast[5] 		= 0x79,
			.h_peaking[0] 		= 0x20, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x30, .h_peaking[3] 		= 0x30, .h_peaking[4] 		= 0x30, .h_peaking[5] 		= 0x30,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x91, .c_filter[2]	 	= 0x91, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x01, .hue[3] 			= 0x01, .hue[4] 			= 0x01, .hue[5] 			= 0x01,
			.u_gain[0] 			= 0xd8, .u_gain[1] 			= 0xf0, .u_gain[2] 			= 0x14, .u_gain[3] 			= 0x14, .u_gain[4] 			= 0x14, .u_gain[5] 			= 0x14,
			.v_gain[0] 			= 0xe0, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0x14, .v_gain[3] 			= 0x14, .v_gain[4] 			= 0x14, .v_gain[5] 			= 0x14,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xf6, .u_offset[3] 		= 0xf6, .u_offset[4] 		= 0xf6, .u_offset[5] 		= 0xf6,
			.v_offset[0] 		= 0xfc, .v_offset[1] 		= 0xfc, .v_offset[2] 		= 0xfc, .v_offset[3] 		= 0xfc, .v_offset[4] 		= 0xfc, .v_offset[5] 		= 0xfc,

			.black_level[0] 	= 0x85, .black_level[1] 	= 0x85, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8b, .black_level[4] 	= 0x8b, .black_level[5] 	= 0x8b,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0x90, .sub_saturation[2] 	= 0x90, .sub_saturation[3] 	= 0x80, .sub_saturation[4] 	= 0x48, .sub_saturation[5] 	= 0x20,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0xc0, .c_option[1] 		= 0xc0, .c_option[2] 		= 0xc0, .c_option[3] 		= 0xc0, .c_option[4] 		= 0xc0, .c_option[5] 		= 0xc0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,

		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]   = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]   = 0x89, .h_scaler2[1]   = 0x89, .h_scaler2[2]   = 0x89, .h_scaler2[3]   = 0x89, .h_scaler2[4]   = 0x89, .h_scaler2[5]   = 0x89,
			.h_scaler3[0]   = 0xc0, .h_scaler3[1]   = 0xc0, .h_scaler3[2]   = 0xc0, .h_scaler3[3]   = 0xc0, .h_scaler3[4]   = 0xc0, .h_scaler3[5]   = 0xc0,
			.h_scaler4[0]   = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01,
			.h_scaler5[0]   = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]   = 0x01, .h_scaler6[1]   = 0x01, .h_scaler6[2]   = 0x01, .h_scaler6[3]   = 0x01, .h_scaler6[4]   = 0x01, .h_scaler6[5]   = 0x01,
			.h_scaler7[0]   = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]   = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]   = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,
			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			.format_set2[0] = 0x0a, .format_set2[1] = 0x0a, .format_set2[2] = 0x0a, .format_set2[3] = 0x0a, .format_set2[4] = 0x0a, .format_set2[5] = 0x0a,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_FHD_25P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x07, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x73, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7f, .eq_gain_sel[2] = 0x6f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7c, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x95, .deq_a_sel[5] 	= 0x93,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x84, .contrast[1] 		= 0x84, .contrast[2] 		= 0x84, .contrast[3] 		= 0x84, .contrast[4] 		= 0x84, .contrast[5] 		= 0x84,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfc, .hue[1] 			= 0xfc, .hue[2] 			= 0xfc, .hue[3] 			= 0xfc, .hue[4] 			= 0xfc, .hue[5] 			= 0xfc,
			.u_gain[0] 			= 0x0c, .u_gain[1] 			= 0x0c, .u_gain[2] 			= 0x0c, .u_gain[3] 			= 0x0c, .u_gain[4] 			= 0x40, .u_gain[5] 			= 0x40,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04,
			.v_offset[0] 		= 0xfc, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfc, .v_offset[3] 		= 0xfc, .v_offset[4] 		= 0xfc, .v_offset[5] 		= 0xfc,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x80, .black_level[2] 	= 0x88, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x84,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa0, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0x90, .sub_saturation[4] 	= 0xc0, .sub_saturation[5] 	= 0xa0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xa0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x7b, .h_delay_a[1] = 0x7b, .h_delay_a[2] = 0x7b, .h_delay_a[3] = 0x7b, .h_delay_a[4] = 0x7b, .h_delay_a[5] = 0x7b,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]  = 0x49, .h_scaler2[1]   = 0x49, .h_scaler2[2]   = 0x49, .h_scaler2[3]   = 0x49, .h_scaler2[4]   = 0x49, .h_scaler2[5]   = 0x49,
			.h_scaler3[0]  = 0x50, .h_scaler3[1]   = 0x50, .h_scaler3[2]   = 0x50, .h_scaler3[3]   = 0x50, .h_scaler3[4]   = 0x50, .h_scaler3[5]   = 0x50,
			.h_scaler4[0]  = 0x02, .h_scaler4[1]   = 0x02, .h_scaler4[2]   = 0x02, .h_scaler4[3]   = 0x02, .h_scaler4[4]   = 0x02, .h_scaler4[5]   = 0x02,
			.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80,

			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_FHD_30P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x07, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x73, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7f, .eq_gain_sel[2] = 0x6f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7c, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x95, .deq_a_sel[5] 	= 0x93,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x84, .contrast[1] 		= 0x84, .contrast[2] 		= 0x84, .contrast[3] 		= 0x84, .contrast[4] 		= 0x84, .contrast[5] 		= 0x84,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfc, .hue[1] 			= 0xfc, .hue[2] 			= 0xfc, .hue[3] 			= 0xfc, .hue[4] 			= 0xfc, .hue[5] 			= 0xfc,
			.u_gain[0] 			= 0x0c, .u_gain[1] 			= 0x0c, .u_gain[2] 			= 0x0c, .u_gain[3] 			= 0x0c, .u_gain[4] 			= 0x40, .u_gain[5] 			= 0x40,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04,
			.v_offset[0] 		= 0xfc, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfc, .v_offset[3] 		= 0xfc, .v_offset[4] 		= 0xfc, .v_offset[5] 		= 0xfc,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x80, .black_level[2] 	= 0x88, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x84,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa0, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0x90, .sub_saturation[4] 	= 0xc0, .sub_saturation[5] 	= 0xa0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xa0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,

		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x88, .h_delay_a[2] = 0x88, .h_delay_a[3] = 0x88, .h_delay_a[4] = 0x88, .h_delay_a[5] = 0x88,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]   = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]   = 0x39, .h_scaler2[1]   = 0x39, .h_scaler2[2]   = 0x39, .h_scaler2[3]   = 0x39, .h_scaler2[4]   = 0x39, .h_scaler2[5]   = 0x39,
			.h_scaler3[0]   = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			.h_scaler4[0]   = 0x02, .h_scaler4[1]   = 0x02, .h_scaler4[2]   = 0x02, .h_scaler4[3]   = 0x02, .h_scaler4[4]   = 0x02, .h_scaler4[5]   = 0x02,
			.h_scaler5[0]   = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]   = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			.h_scaler7[0]   = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]   = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]   = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80,
			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ TVI_4M_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x73, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x37, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x94, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x7c, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x70, .h_peaking[3] 		= 0x70, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x60, .u_gain[1] 			= 0x60, .u_gain[2] 			= 0x60, .u_gain[3] 			= 0x60, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x30, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x70, .v_gain[5] 			= 0x70,
			.u_offset[0] 		= 0x0f, .u_offset[1] 		= 0x0f, .u_offset[2] 		= 0x28, .u_offset[3] 		= 0x0f, .u_offset[4] 		= 0x0f, .u_offset[5] 		= 0x0f,
			.v_offset[0] 		= 0xf0, .v_offset[1] 		= 0xf0, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0xf0, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf0,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x8c, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x47, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x20, .acc_ref[5]			= 0x20,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xe0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xc0, .sub_saturation[4] 	= 0xc0, .sub_saturation[5] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0xa0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x79, .h_delay_a[1] = 0x7b, .h_delay_a[2] = 0x7b, .h_delay_a[3] = 0x7b, .h_delay_a[4] = 0x7b, .h_delay_a[5] = 0x7b,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0f, .format_set1[1] = 0x0f, .format_set1[2] = 0x0f, .format_set1[3] = 0x0f, .format_set1[4] = 0x0f, .format_set1[5] = 0x0f,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ TVI_4M_30P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x73, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x37, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x94, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x7c, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x70, .h_peaking[3] 		= 0x70, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x60, .u_gain[1] 			= 0x60, .u_gain[2] 			= 0x60, .u_gain[3] 			= 0x60, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x30, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x70, .v_gain[5] 			= 0x70,
			.u_offset[0] 		= 0x0f, .u_offset[1] 		= 0x0f, .u_offset[2] 		= 0x28, .u_offset[3] 		= 0x0f, .u_offset[4] 		= 0x0f, .u_offset[5] 		= 0x0f,
			.v_offset[0] 		= 0xf0, .v_offset[1] 		= 0xf0, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0xf0, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf0,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x8c, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x47, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x20, .acc_ref[5]			= 0x20,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xe0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xc0, .sub_saturation[4] 	= 0xc0, .sub_saturation[5] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0xa0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x72, .h_delay_a[1] = 0x75, .h_delay_a[2] = 0x75, .h_delay_a[3] = 0x75, .h_delay_a[4] = 0x75, .h_delay_a[5] = 0x75,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0e, .format_set1[1] = 0x0e, .format_set1[2] = 0x0e, .format_set1[3] = 0x0e, .format_set1[4] = 0x0e, .format_set1[5] = 0x0e,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ TVI_5M_20P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x37, .eq_band_sel[5] = 0x37,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7f, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x6e, .contrast[1] 		= 0x6b, .contrast[2] 		= 0x98, .contrast[3] 		= 0x66, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x10, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0xa2, .c_filter[2]	 	= 0xb2, .c_filter[3] 		= 0xb2, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x02, .hue[3] 			= 0x02, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x28, .u_gain[2] 			= 0x34, .u_gain[3] 			= 0x34, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0x3a, .v_gain[1] 			= 0x38, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x40, .v_gain[4] 			= 0x70, .v_gain[5] 			= 0x70,
			.u_offset[0] 		= 0x02, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x0f, .u_offset[5] 		= 0x0f,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf0,

			.black_level[0] 	= 0x87, .black_level[1] 	= 0x87, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x20, .acc_ref[5]			= 0x20,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0xa0, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0x80, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xb0, .sub_saturation[3] 	= 0x9c, .sub_saturation[4] 	= 0x60, .sub_saturation[5] 	= 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0xa0, .c_option[2] 		= 0xb0, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x7b, .h_delay_a[1] = 0x7b, .h_delay_a[2] = 0x7b, .h_delay_a[3] = 0x7b, .h_delay_a[4] = 0x7b, .h_delay_a[5] = 0x7b,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0c, .format_set1[1] = 0x0c, .format_set1[2] = 0x0c, .format_set1[3] = 0x0c, .format_set1[4] = 0x0c, .format_set1[5] = 0x0c,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x22, .v_delay[1]     = 0x22, .v_delay[2]     = 0x22, .v_delay[3]     = 0x22, .v_delay[4]     = 0x22, .v_delay[5]     = 0x22,
		},
	},


	[ TVI_3M_18P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x47, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7e, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x6e, .contrast[1] 		= 0x6b, .contrast[2] 		= 0x98, .contrast[3] 		= 0x66, .contrast[4] 		= 0x61, .contrast[5] 		= 0x65,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x10, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x30, .h_peaking[5] 		= 0x20,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xc4, .c_filter[5] 		= 0xc4,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x02, .hue[3] 			= 0x02, .hue[4] 			= 0x02, .hue[5] 			= 0x02,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x28, .u_gain[2] 			= 0x34, .u_gain[3] 			= 0x34, .u_gain[4] 			= 0x34, .u_gain[5] 			= 0x34,
			.v_gain[0] 			= 0x3a, .v_gain[1] 			= 0x38, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x40, .v_gain[4] 			= 0x40, .v_gain[5] 			= 0x40,
			.u_offset[0] 		= 0x02, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x87, .black_level[1] 	= 0x87, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x8d, .black_level[5] 	= 0x8e,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0xa0, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0x80, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0x80, .sub_saturation[3] 	= 0x9c, .sub_saturation[4] 	= 0x00, .sub_saturation[5] 	= 0xa0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x70, .h_delay_a[1] = 0x78, .h_delay_a[2] = 0x75, .h_delay_a[3] = 0x75, .h_delay_a[4] = 0x75, .h_delay_a[5] = 0x75,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x05, .h_delay_c[1] = 0x05, .h_delay_c[2] = 0x05, .h_delay_c[3] = 0x05, .h_delay_c[4] = 0x05, .h_delay_c[5] = 0x05,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x08, .format_set1[1] = 0x08, .format_set1[2] = 0x08, .format_set1[3] = 0x08, .format_set1[4] = 0x08, .format_set1[5] = 0x08,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x22, .v_delay[1]     = 0x22, .v_delay[2]     = 0x22, .v_delay[3]     = 0x22, .v_delay[4]     = 0x22, .v_delay[5]     = 0x22,
		},
	},
	[ TVI_4M_15P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x47, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7e, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x6e, .contrast[1] 		= 0x6b, .contrast[2] 		= 0x98, .contrast[3] 		= 0x66, .contrast[4] 		= 0x61, .contrast[5] 		= 0x65,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x10, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x30, .h_peaking[5] 		= 0x20,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xc4, .c_filter[5] 		= 0xc4,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x02, .hue[3] 			= 0x02, .hue[4] 			= 0x02, .hue[5] 			= 0x02,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x28, .u_gain[2] 			= 0x34, .u_gain[3] 			= 0x34, .u_gain[4] 			= 0x34, .u_gain[5] 			= 0x34,
			.v_gain[0] 			= 0x3a, .v_gain[1] 			= 0x38, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x40, .v_gain[4] 			= 0x40, .v_gain[5] 			= 0x40,
			.u_offset[0] 		= 0x02, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x87, .black_level[1] 	= 0x87, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x8d, .black_level[5] 	= 0x8e,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0xa0, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0x80, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0x80, .sub_saturation[3] 	= 0x9c, .sub_saturation[4] 	= 0x00, .sub_saturation[5] 	= 0xa0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x7e, .h_delay_a[3] = 0x7e, .h_delay_a[4] = 0x7e, .h_delay_a[5] = 0x7e,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0d, .format_set1[1] = 0x0d, .format_set1[2] = 0x0d, .format_set1[3] = 0x0d, .format_set1[4] = 0x0d, .format_set1[5] = 0x0d,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},



	[ TVI_5M_12_5P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x27,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x6e, .contrast[1] 		= 0x6b, .contrast[2] 		= 0x98, .contrast[3] 		= 0x66, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x10, .h_peaking[1] 		= 0x10, .h_peaking[2] 		= 0x10, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x81, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x02, .hue[3] 			= 0x02, .hue[4] 			= 0x05, .hue[5] 			= 0x05,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x28, .u_gain[2] 			= 0x34, .u_gain[3] 			= 0x34, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0x3a, .v_gain[1] 			= 0x38, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x40, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0x02, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x87, .black_level[1] 	= 0x87, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x86, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x27, .acc_ref[5]			= 0x40,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0xa0, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0x80, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0x80, .sub_saturation[3] 	= 0x9c, .sub_saturation[4] 	= 0x60, .sub_saturation[5] 	= 0x80,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x00, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x78, .h_delay_a[1] = 0x82, .h_delay_a[2] = 0x82, .h_delay_a[3] = 0x82, .h_delay_a[4] = 0x82, .h_delay_a[5] = 0x82,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x09, .format_set1[1] = 0x09, .format_set1[2] = 0x09, .format_set1[3] = 0x09, .format_set1[4] = 0x09, .format_set1[5] = 0x09,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20,
		},
	},

	[ AHD30_4M_15P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xfb,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x85, .black_level[5] 	= 0x87,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x78, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0A, .format_set1[1] = 0x0A, .format_set1[2] = 0x0A, .format_set1[3] = 0x0A, .format_set1[4] = 0x0A, .format_set1[5] = 0x0A,
			/*B0 0x85*/	.format_set2[0] = 0x04, .format_set2[1] = 0x04, .format_set2[2] = 0x04, .format_set2[3] = 0x04, .format_set2[4] = 0x04, .format_set2[5] = 0x04,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD30_4M_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x94, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0xa0, .contrast[1] 		= 0x9c, .contrast[2] 		= 0x99, .contrast[3] 		= 0x95, .contrast[4] 		= 0x95, .contrast[5] 		= 0x8a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfc, .hue[1] 			= 0xfc, .hue[2] 			= 0xfc, .hue[3] 			= 0xfc, .hue[4] 			= 0xfc, .hue[5] 			= 0xfc,
			.u_gain[0] 			= 0x20, .u_gain[1] 			= 0x20, .u_gain[2] 			= 0x20, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x40, .v_gain[1] 			= 0x40, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x20, .v_gain[4] 			= 0x20, .v_gain[5] 			= 0x20,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x8a, .black_level[2] 	= 0x82, .black_level[3] 	= 0x85, .black_level[4] 	= 0x85, .black_level[5] 	= 0x85,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x72, .h_delay_a[1] = 0x78, .h_delay_a[2] = 0x7a, .h_delay_a[3] = 0x7a, .h_delay_a[4] = 0x7a, .h_delay_a[5] = 0x7a,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0f, .format_set1[1] = 0x0f, .format_set1[2] = 0x0f, .format_set1[3] = 0x0f, .format_set1[4] = 0x0f, .format_set1[5] = 0x0f,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ AHD30_4M_30P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x94, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0xa0, .contrast[1] 		= 0x9c, .contrast[2] 		= 0x99, .contrast[3] 		= 0x95, .contrast[4] 		= 0x95, .contrast[5] 		= 0x8a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfc, .hue[1] 			= 0xfc, .hue[2] 			= 0xfc, .hue[3] 			= 0xfc, .hue[4] 			= 0xfc, .hue[5] 			= 0xfc,
			.u_gain[0] 			= 0x20, .u_gain[1] 			= 0x20, .u_gain[2] 			= 0x20, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x40, .v_gain[1] 			= 0x40, .v_gain[2] 			= 0x40, .v_gain[3] 			= 0x20, .v_gain[4] 			= 0x20, .v_gain[5] 			= 0x20,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x8a, .black_level[2] 	= 0x82, .black_level[3] 	= 0x85, .black_level[4] 	= 0x85, .black_level[5] 	= 0x85,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xb0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x73, .h_delay_a[1] = 0x76, .h_delay_a[2] = 0x76, .h_delay_a[3] = 0x76, .h_delay_a[4] = 0x76, .h_delay_a[5] = 0x76,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0e, .format_set1[1] = 0x0e, .format_set1[2] = 0x0e, .format_set1[3] = 0x0e, .format_set1[4] = 0x0e, .format_set1[5] = 0x0e,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD30_5M_12_5P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xfb,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x85, .black_level[5] 	= 0x87,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x75, .h_delay_a[1] = 0x78, .h_delay_a[2] = 0x78, .h_delay_a[3] = 0x78, .h_delay_a[4] = 0x78, .h_delay_a[5] = 0x78,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x00, .format_set1[1] = 0x00, .format_set1[2] = 0x00, .format_set1[3] = 0x00, .format_set1[4] = 0x00, .format_set1[5] = 0x00,
			/*B0 0x85*/	.format_set2[0] = 0x05, .format_set2[1] = 0x05, .format_set2[2] = 0x05, .format_set2[3] = 0x05, .format_set2[4] = 0x05, .format_set2[5] = 0x05,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},


	[ AHD30_5M_20P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x8b, .contrast[1] 		= 0x87, .contrast[2] 		= 0x8b, .contrast[3] 		= 0x80, .contrast[4] 		= 0x7b, .contrast[5] 		= 0x75,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x70, .u_gain[1] 			= 0x70, .u_gain[2] 			= 0x64, .u_gain[3] 			= 0x5c, .u_gain[4] 			= 0x5c, .u_gain[5] 			= 0x5c,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfc, .u_offset[1] 		= 0xfc, .u_offset[2] 		= 0xfc, .u_offset[3] 		= 0xfc, .u_offset[4] 		= 0xfc, .u_offset[5] 		= 0xfc,
			.v_offset[0] 		= 0xf7, .v_offset[1] 		= 0xf7, .v_offset[2] 		= 0xf7, .v_offset[3] 		= 0xf7, .v_offset[4] 		= 0xf7, .v_offset[5] 		= 0xf7,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x80, .black_level[2] 	= 0x80, .black_level[3] 	= 0x80, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xe8, .sub_saturation[1] 	= 0xe8, .sub_saturation[2] 	= 0xd8, .sub_saturation[3] 	= 0xf8, .sub_saturation[4] 	= 0xe8, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0xa0, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x7b, .h_delay_a[1] = 0x7b, .h_delay_a[2] = 0x7b, .h_delay_a[3] = 0x7b, .h_delay_a[4] = 0x7b, .h_delay_a[5] = 0x7b,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x01, .format_set1[1] = 0x01, .format_set1[2] = 0x01, .format_set1[3] = 0x01, .format_set1[4] = 0x01, .format_set1[5] = 0x01,
			/*B0 0x85*/	.format_set2[0] = 0x05, .format_set2[1] = 0x05, .format_set2[2] = 0x05, .format_set2[3] = 0x05, .format_set2[4] = 0x05, .format_set2[5] = 0x05,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD30_5_3M_20P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x8b, .contrast[1] 		= 0x87, .contrast[2] 		= 0x8b, .contrast[3] 		= 0x80, .contrast[4] 		= 0x7b, .contrast[5] 		= 0x75,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x70, .u_gain[1] 			= 0x70, .u_gain[2] 			= 0x64, .u_gain[3] 			= 0x5c, .u_gain[4] 			= 0x5c, .u_gain[5] 			= 0x5c,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfc, .u_offset[1] 		= 0xfc, .u_offset[2] 		= 0xfc, .u_offset[3] 		= 0xfc, .u_offset[4] 		= 0xfc, .u_offset[5] 		= 0xfc,
			.v_offset[0] 		= 0xf7, .v_offset[1] 		= 0xf7, .v_offset[2] 		= 0xf7, .v_offset[3] 		= 0xf7, .v_offset[4] 		= 0xf7, .v_offset[5] 		= 0xf7,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x80, .black_level[2] 	= 0x80, .black_level[3] 	= 0x80, .black_level[4] 	= 0x80, .black_level[5] 	= 0x80,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xe8, .sub_saturation[1] 	= 0xe8, .sub_saturation[2] 	= 0xd8, .sub_saturation[3] 	= 0xf8, .sub_saturation[4] 	= 0xe8, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0xa0, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x7f, .h_delay_a[4] = 0x7f, .h_delay_a[5] = 0x7f,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			/*B0 0x85*/	.format_set2[0] = 0x05, .format_set2[1] = 0x05, .format_set2[2] = 0x05, .format_set2[3] = 0x05, .format_set2[4] = 0x05, .format_set2[5] = 0x05,

			/*B0 0x64*/ .v_delay[0]     = 0x24, .v_delay[1]     = 0x24, .v_delay[2]     = 0x24, .v_delay[3]     = 0x24, .v_delay[4]     = 0x24, .v_delay[5]     = 0x24,
		},
	},

	[ AHD30_3M_18P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xfb,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x85, .black_level[5] 	= 0x87,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x63, .h_delay_a[1] = 0x65, .h_delay_a[2] = 0x65, .h_delay_a[3] = 0x65, .h_delay_a[4] = 0x65, .h_delay_a[5] = 0x65,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x04, .format_set1[1] = 0x04, .format_set1[2] = 0x04, .format_set1[3] = 0x04, .format_set1[4] = 0x04, .format_set1[5] = 0x04,
			/*B0 0x85*/	.format_set2[0] = 0x04, .format_set2[1] = 0x04, .format_set2[2] = 0x04, .format_set2[3] = 0x04, .format_set2[4] = 0x04, .format_set2[5] = 0x04,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD30_3M_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x79, .contrast[3] 		= 0x75, .contrast[4] 		= 0x70, .contrast[5] 		= 0x7a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x82, .black_level[2] 	= 0x82, .black_level[3] 	= 0x85, .black_level[4] 	= 0x89, .black_level[5] 	= 0x8c,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xb0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xb4, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			/*B0 0x85*/	.format_set2[0] = 0x04, .format_set2[1] = 0x04, .format_set2[2] = 0x04, .format_set2[3] = 0x04, .format_set2[4] = 0x04, .format_set2[5] = 0x04,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ AHD30_3M_30P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x7c, .contrast[2] 		= 0x79, .contrast[3] 		= 0x75, .contrast[4] 		= 0x70, .contrast[5] 		= 0x7a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x82, .black_level[2] 	= 0x82, .black_level[3] 	= 0x85, .black_level[4] 	= 0x89, .black_level[5] 	= 0x8c,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xb0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xb4, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			/*B0 0x85*/	.format_set2[0] = 0x04, .format_set2[1] = 0x04, .format_set2[2] = 0x04, .format_set2[3] = 0x04, .format_set2[4] = 0x04, .format_set2[5] = 0x04,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},


	[ AHD20_1080P_25P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x78, .h_delay_a[1] = 0x7a, .h_delay_a[2] = 0x7a, .h_delay_a[3] = 0x7a, .h_delay_a[4] = 0x7a, .h_delay_a[5] = 0x7a,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ AHD20_1080P_30P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x70, .h_delay_a[1] = 0x78, .h_delay_a[2] = 0x70, .h_delay_a[3] = 0x70, .h_delay_a[4] = 0x70, .h_delay_a[5] = 0x70,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ AHD20_1080P_12_5P_EX ] = { /* o */
	/* base */
	{
		.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,      // Bank5 0x30
		.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
		.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
		.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
		.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x94, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
		.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
	},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0x10, .u_gain[4] 			= 0x10, .u_gain[5] 			= 0x18,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0x0e, .v_gain[4] 			= 0x0e, .v_gain[5] 			= 0x14,
			.u_offset[0] 		= 0xfe, .u_offset[1] 		= 0xfe, .u_offset[2] 		= 0xfe, .u_offset[3] 		= 0xfe, .u_offset[4] 		= 0xfe, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xfb, .v_offset[1] 		= 0xfb, .v_offset[2] 		= 0xfb, .v_offset[3] 		= 0xfb, .v_offset[4] 		= 0xfb, .v_offset[5] 		= 0xfb,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x83, .black_level[5] 	= 0x87,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x30, .burst_dec_c[1] 	= 0x30, .burst_dec_c[2] 	= 0x30, .burst_dec_c[3] 	= 0x30, .burst_dec_c[4] 	= 0x30, .burst_dec_c[5] 	= 0x30,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x03, .y_delay[2]   = 0x03, .y_delay[3]   = 0x03, .y_delay[4]   = 0x03, .y_delay[5] =   0x03,

		},
		/* clk */
		{
			
		.clk_adc[0] = 0x0a, .clk_adc[1] = 0x0a, .clk_adc[2] = 0x0a, .clk_adc[3] = 0x0a, .clk_adc[4] = 0x0a, .clk_adc[5] = 0x0a,
		.clk_dec[0] = 0x4a, .clk_dec[1] = 0x4a, .clk_dec[2] = 0x4a, .clk_dec[3] = 0x4a, .clk_dec[4] = 0x4a, .clk_dec[5] = 0x4a,
		},
		/* timing_b */
		{
	/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
	/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
	/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
	/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
	/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
	/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
	/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
	/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
	/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


	/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

	/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
	/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
	/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			    .fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

	/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
	/*B0 0x85*/	.format_set2[0] = 0x10, .format_set2[1] = 0x10, .format_set2[2] = 0x10, .format_set2[3] = 0x10, .format_set2[4] = 0x10, .format_set2[5] = 0x10,

	/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20,
		},
	},
	[ AHD20_1080P_15P_EX ] = { /* o */
	/* base */
	{
		.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,      // Bank5 0x30
		.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
		.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
		.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
		.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x94, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
		.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
	},
	/* coeff */
	{
		.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
		.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
		.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
		.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
		.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
		.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
		.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
		.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
		.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
		.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
		.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
		.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
	},
	/* color */
	{
		.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
		.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
		.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

		.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
		.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0x10, .u_gain[4] 			= 0x10, .u_gain[5] 			= 0x18,
		.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0x0e, .v_gain[4] 			= 0x0e, .v_gain[5] 			= 0x14,
		.u_offset[0] 		= 0xfe, .u_offset[1] 		= 0xfe, .u_offset[2] 		= 0xfe, .u_offset[3] 		= 0xfe, .u_offset[4] 		= 0xfe, .u_offset[5] 		= 0xfe,
		.v_offset[0] 		= 0xfb, .v_offset[1] 		= 0xfb, .v_offset[2] 		= 0xfb, .v_offset[3] 		= 0xfb, .v_offset[4] 		= 0xfb, .v_offset[5] 		= 0xfb,

		.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x83, .black_level[5] 	= 0x87,
		.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
		.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
		.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

		.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
		.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
		.burst_dec_c[0] 	= 0x30, .burst_dec_c[1] 	= 0x30, .burst_dec_c[2] 	= 0x30, .burst_dec_c[3] 	= 0x30, .burst_dec_c[4] 	= 0x30, .burst_dec_c[5] 	= 0x30,

		.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

		.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
		.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
	},
	/* timing_a */
	{
		.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
		.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
		.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
		.y_delay[0]   = 0x05, .y_delay[1]   = 0x03, .y_delay[2]   = 0x03, .y_delay[3]   = 0x03, .y_delay[4]   = 0x03, .y_delay[5] =   0x03,

	},
	/* clk */
	{
		.clk_adc[0] = 0x0a, .clk_adc[1] = 0x0a, .clk_adc[2] = 0x0a, .clk_adc[3] = 0x0a, .clk_adc[4] = 0x0a, .clk_adc[5] = 0x0a,
		.clk_dec[0] = 0x4a, .clk_dec[1] = 0x4a, .clk_dec[2] = 0x4a, .clk_dec[3] = 0x4a, .clk_dec[4] = 0x4a, .clk_dec[5] = 0x4a,
	},
	/* timing_b */
	{
	/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
	/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
	/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
	/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
	/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
	/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
	/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
	/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
	/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


	/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

	/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
	/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
	/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
				.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

	/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
	/*B0 0x85*/	.format_set2[0] = 0x10, .format_set2[1] = 0x10, .format_set2[2] = 0x10, .format_set2[3] = 0x10, .format_set2[4] = 0x10, .format_set2[5] = 0x10,

	/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20,
		},
	},

	[ AHD20_1080P_50P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x8e, .h_delay_a[2] = 0x8a, .h_delay_a[3] = 0x86, .h_delay_a[4] = 0x86, .h_delay_a[5] = 0x86,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD20_1080P_60P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x8e, .h_delay_a[2] = 0x8a, .h_delay_a[3] = 0x86, .h_delay_a[4] = 0x86, .h_delay_a[5] = 0x86,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD30_8M_7_5P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x86, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x7e, .contrast[4] 		= 0x7c, .contrast[5] 		= 0x77,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfe,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xfb,

			.black_level[0] 	= 0x80, .black_level[1] 	= 0x81, .black_level[2] 	= 0x81, .black_level[3] 	= 0x83, .black_level[4] 	= 0x85, .black_level[5] 	= 0x87,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa8, .sub_saturation[2] 	= 0xa8, .sub_saturation[3] 	= 0xa8, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x90, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x03, .y_delay[2]   = 0x03, .y_delay[3]   = 0x03, .y_delay[4]   = 0x03, .y_delay[5] =   0x03,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x00, .format_set1[1] = 0x00, .format_set1[2] = 0x00, .format_set1[3] = 0x00, .format_set1[4] = 0x00, .format_set1[5] = 0x00,
			/*B0 0x85*/	.format_set2[0] = 0x08, .format_set2[1] = 0x08, .format_set2[2] = 0x08, .format_set2[3] = 0x08, .format_set2[4] = 0x08, .format_set2[5] = 0x08,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20,
		},
	},

	[ AHD30_8M_12_5P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0xa0, .contrast[1] 		= 0x9c, .contrast[2] 		= 0x90, .contrast[3] 		= 0x95, .contrast[4] 		= 0x90, .contrast[5] 		= 0x8a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x82, .black_level[2] 	= 0x84, .black_level[3] 	= 0x85, .black_level[4] 	= 0x89, .black_level[5] 	= 0x8c,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xb0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xb4, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x7f, .h_delay_a[4] = 0x7f, .h_delay_a[5] = 0x7f,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x01, .format_set1[1] = 0x01, .format_set1[2] = 0x01, .format_set1[3] = 0x01, .format_set1[4] = 0x01, .format_set1[5] = 0x01,
			/*B0 0x85*/	.format_set2[0] = 0x08, .format_set2[1] = 0x08, .format_set2[2] = 0x08, .format_set2[3] = 0x08, .format_set2[4] = 0x08, .format_set2[5] = 0x08,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ AHD30_8M_15P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7b, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7e, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x98, .deq_a_sel[4] 	= 0x98, .deq_a_sel[5] 	= 0x98,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x20, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0xa0, .contrast[1] 		= 0x9c, .contrast[2] 		= 0x90, .contrast[3] 		= 0x95, .contrast[4] 		= 0x90, .contrast[5] 		= 0x8a,
			.h_peaking[0] 		= 0x0f, .h_peaking[1] 		= 0x0f, .h_peaking[2] 		= 0x0f, .h_peaking[3] 		= 0x0f, .h_peaking[4] 		= 0x0f, .h_peaking[5] 		= 0x0f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x04, .hue[1] 			= 0x04, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x10, .u_gain[1] 			= 0x10, .u_gain[2] 			= 0x04, .u_gain[3] 			= 0xfc, .u_gain[4] 			= 0xfc, .u_gain[5] 			= 0xfc,
			.v_gain[0] 			= 0x10, .v_gain[1] 			= 0x10, .v_gain[2] 			= 0x04, .v_gain[3] 			= 0xf4, .v_gain[4] 			= 0x08, .v_gain[5] 			= 0x08,
			.u_offset[0] 		= 0xfb, .u_offset[1] 		= 0xfb, .u_offset[2] 		= 0xfb, .u_offset[3] 		= 0xfb, .u_offset[4] 		= 0xfb, .u_offset[5] 		= 0xfb,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x82, .black_level[2] 	= 0x84, .black_level[3] 	= 0x85, .black_level[4] 	= 0x89, .black_level[5] 	= 0x8c,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xb0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xd0, .sub_saturation[4] 	= 0xb4, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x6c, .h_delay_a[1] = 0x6c, .h_delay_a[2] = 0x6c, .h_delay_a[3] = 0x6c, .h_delay_a[4] = 0x6c, .h_delay_a[5] = 0x6c,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x02, .h_delay_c[1] = 0x02, .h_delay_c[2] = 0x02, .h_delay_c[3] = 0x02, .h_delay_c[4] = 0x02, .h_delay_c[5] = 0x02,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			/*B0 0x85*/	.format_set2[0] = 0x08, .format_set2[1] = 0x08, .format_set2[2] = 0x08, .format_set2[3] = 0x08, .format_set2[4] = 0x08, .format_set2[5] = 0x08,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ TVI_FHD_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x27,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x82, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x05, .hue[1] 			= 0x05, .hue[2] 			= 0x05, .hue[3] 			= 0x05, .hue[4] 			= 0x05, .hue[5] 			= 0x05,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0xf0, .v_gain[1] 			= 0xf0, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x88, .black_level[1] 	= 0x8c, .black_level[2] 	= 0x8a, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x86, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x40, .acc_ref[5]			= 0x40,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xe0, .sub_saturation[3] 	= 0xe0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0xd0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x70, .h_delay_a[1] = 0x70, .h_delay_a[2] = 0x72, .h_delay_a[3] = 0x75, .h_delay_a[4] = 0x7b, .h_delay_a[5] = 0x7b,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x03, .format_set1[1] = 0x03, .format_set1[2] = 0x03, .format_set1[3] = 0x03, .format_set1[4] = 0x03, .format_set1[5] = 0x03,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ TVI_FHD_30P ] =
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x27,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x82, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x05, .hue[1] 			= 0x05, .hue[2] 			= 0x05, .hue[3] 			= 0x05, .hue[4] 			= 0x05, .hue[5] 			= 0x05,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70,
			.v_gain[0] 			= 0xf0, .v_gain[1] 			= 0xf0, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf8,

			.black_level[0] 	= 0x88, .black_level[1] 	= 0x8c, .black_level[2] 	= 0x8a, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x86, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x40, .acc_ref[5]			= 0x40,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xe0, .sub_saturation[3] 	= 0xe0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0xd0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x70, .h_delay_a[1] = 0x70, .h_delay_a[2] = 0x73, .h_delay_a[3] = 0x75, .h_delay_a[4] = 0x75, .h_delay_a[5] = 0x75,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},


	[ AHD20_720P_25P_EX_Btype ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x47, .eq_band_sel[7] = 0x47, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x17, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x94, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x8d, .deq_a_sel[10] 	 = 0x8d, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x78, .contrast[7] 		= 0x74, .contrast[8] 		= 0x74, .contrast[9] 		= 0x74, .contrast[10] 		= 0x74,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f, .h_peaking[6] 		= 0x3f, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x4f, .h_peaking[10] 		= 0x4f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe, .hue[6] 			= 0xfe, .hue[7] 			= 0xfe, .hue[8] 			= 0xfe, .hue[9] 			= 0xfe, .hue[10] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30, .u_gain[6] 			= 0x30, .u_gain[7] 			= 0x30, .u_gain[8] 			= 0x30, .u_gain[9] 			= 0x30, .u_gain[10] 		= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x90, .black_level[10] 	= 0x90,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x57, .acc_ref[8]			= 0x57, .acc_ref[9]			= 0x47, .acc_ref[10]		= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x90, .cti_delay[10]		= 0x90,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xab, .sub_saturation[2] 	= 0xb0, .sub_saturation[3] 	= 0xb0, .sub_saturation[4] 	= 0xa4, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x90, .sub_saturation[10] = 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90, .c_option[6] 		= 0x90, .c_option[7] 		= 0x90, .c_option[8] 		= 0x90, .c_option[9] 		= 0x90, .c_option[10] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0xa0, .h_delay_a[2] = 0x9e, .h_delay_a[3] = 0x9b, .h_delay_a[4] = 0x99, .h_delay_a[5] = 0x99, .h_delay_a[6] = 0x9b, .h_delay_a[7] = 0x98, .h_delay_a[8] = 0x98, .h_delay_a[9] = 0x98, .h_delay_a[10] = 0x98,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01, .comb_mode[6]	= 0x01, .comb_mode[7]   = 0x01, .comb_mode[8]   = 0x01, .comb_mode[9]  	= 0x01, .comb_mode[10]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00, .mem_path[6]	= 0x00, .mem_path[7]	= 0x00, .mem_path[8]	= 0x00, .mem_path[9]	= 0x00, .mem_path[10]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0d, .format_set1[1] = 0x0d, .format_set1[2] = 0x0d, .format_set1[3] = 0x0d, .format_set1[4] = 0x0d, .format_set1[5] = 0x0d, .format_set1[6] = 0x0d, .format_set1[7] = 0x0d, .format_set1[8] = 0x0d, .format_set1[9] = 0x0d, .format_set1[10] = 0x0d,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00, .format_set2[6] = 0x00, .format_set2[7] = 0x00, .format_set2[8] = 0x00, .format_set2[9] = 0x00, .format_set2[10] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},
	[ AHD20_720P_30P_EX_Btype ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x47, .eq_band_sel[7] = 0x47, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x17, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x94, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x8d, .deq_a_sel[10] 	 = 0x8d, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x78, .contrast[7] 		= 0x74, .contrast[8] 		= 0x74, .contrast[9] 		= 0x74, .contrast[10] 		= 0x74,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f, .h_peaking[6] 		= 0x3f, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x4f, .h_peaking[10] 		= 0x4f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe, .hue[6] 			= 0xfe, .hue[7] 			= 0xfe, .hue[8] 			= 0xfe, .hue[9] 			= 0xfe, .hue[10] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30, .u_gain[6] 			= 0x30, .u_gain[7] 			= 0x30, .u_gain[8] 			= 0x30, .u_gain[9] 			= 0x30, .u_gain[10] 		= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x90, .black_level[10] 	= 0x90,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x57, .acc_ref[8]			= 0x57, .acc_ref[9]			= 0x47, .acc_ref[10]		= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x90, .cti_delay[10]		= 0x90,
			.sub_saturation[0] 	= 0xb0, .sub_saturation[1] 	= 0xab, .sub_saturation[2] 	= 0xb0, .sub_saturation[3] 	= 0xb0, .sub_saturation[4] 	= 0xa4, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x90, .sub_saturation[10] = 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90, .c_option[6] 		= 0x90, .c_option[7] 		= 0x90, .c_option[8] 		= 0x90, .c_option[9] 		= 0x90, .c_option[10] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0xa0, .h_delay_a[2] = 0x9e, .h_delay_a[3] = 0x9b, .h_delay_a[4] = 0x99, .h_delay_a[5] = 0x99, .h_delay_a[6] = 0x9b, .h_delay_a[7] = 0x98, .h_delay_a[8] = 0x98, .h_delay_a[9] = 0x98, .h_delay_a[10] = 0x98,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01, .comb_mode[6]	= 0x01, .comb_mode[7]   = 0x01, .comb_mode[8]   = 0x01, .comb_mode[9]  	= 0x01, .comb_mode[10]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00, .mem_path[6]	= 0x00, .mem_path[7]	= 0x00, .mem_path[8]	= 0x00, .mem_path[9]	= 0x00, .mem_path[10]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0c, .format_set1[1] = 0x0c, .format_set1[2] = 0x0c, .format_set1[3] = 0x0c, .format_set1[4] = 0x0c, .format_set1[5] = 0x0c, .format_set1[6] = 0x0c, .format_set1[7] = 0x0c, .format_set1[8] = 0x0c, .format_set1[9] = 0x0c, .format_set1[10] = 0x0c,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00, .format_set2[6] = 0x00, .format_set2[7] = 0x00, .format_set2[8] = 0x00, .format_set2[9] = 0x00, .format_set2[10] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},

	[ AHD20_720P_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x47, .eq_band_sel[7] = 0x47, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x17, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x8e, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x8d, .deq_a_sel[10] 	 = 0x8d, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x78, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x78, .contrast[7] 		= 0x78, .contrast[8] 		= 0x74, .contrast[9] 		= 0x74, .contrast[10] 		= 0x74,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f, .h_peaking[6] 		= 0x3f, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x4f, .h_peaking[10] 		= 0x4f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0x92,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x90, .black_level[10] 	= 0x90,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x57, .acc_ref[8]			= 0x57, .acc_ref[9]			= 0x47, .acc_ref[10]		= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x90, .cti_delay[10]		= 0x90,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xab, .sub_saturation[2] 	= 0xb0, .sub_saturation[3] 	= 0xb0, .sub_saturation[4] 	= 0xa4, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x90, .sub_saturation[10] = 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90, .c_option[6] 		= 0x90, .c_option[7] 		= 0x90, .c_option[8] 		= 0x90, .c_option[9] 		= 0x90, .c_option[10] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0xa0, .h_delay_a[1] = 0xa0, .h_delay_a[2] = 0x9e, .h_delay_a[3] = 0x9b, .h_delay_a[4] = 0x99, .h_delay_a[5] = 0x99, .h_delay_a[6] = 0x9b, .h_delay_a[7] = 0x98, .h_delay_a[8] = 0x98, .h_delay_a[9] = 0x98, .h_delay_a[10] = 0x98,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01, .comb_mode[6]	= 0x01, .comb_mode[7]   = 0x01, .comb_mode[8]   = 0x01, .comb_mode[9]  	= 0x01, .comb_mode[10]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00, .mem_path[6]	= 0x00, .mem_path[7]	= 0x00, .mem_path[8]	= 0x00, .mem_path[9]	= 0x00, .mem_path[10]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x07, .format_set1[1] = 0x07, .format_set1[2] = 0x07, .format_set1[3] = 0x07, .format_set1[4] = 0x07, .format_set1[5] = 0x07, .format_set1[6] = 0x07, .format_set1[7] = 0x07, .format_set1[8] = 0x07, .format_set1[9] = 0x07, .format_set1[10] = 0x07,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00, .format_set2[6] = 0x00, .format_set2[7] = 0x00, .format_set2[8] = 0x00, .format_set2[9] = 0x00, .format_set2[10] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x21, .v_delay[1]     = 0x21, .v_delay[2]     = 0x21, .v_delay[3]     = 0x21, .v_delay[4]     = 0x21, .v_delay[5]     = 0x21, .v_delay[6]     = 0x21, .v_delay[7]     = 0x21, .v_delay[8]     = 0x21, .v_delay[9]     = 0x21, .v_delay[10]     = 0x21,
		},
	},
	[ AHD20_720P_30P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x47, .eq_band_sel[7] = 0x47, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x17, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x86, .deq_a_sel[2] 	= 0x87, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x8e, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x8d, .deq_a_sel[10] 	 = 0x8d, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x78, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x78, .contrast[7] 		= 0x78, .contrast[8] 		= 0x74, .contrast[9] 		= 0x74, .contrast[10] 		= 0x74,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f, .h_peaking[6] 		= 0x3f, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x4f, .h_peaking[10] 		= 0x4f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0x92,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x90, .black_level[10] 	= 0x90,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x57, .acc_ref[8]			= 0x57, .acc_ref[9]			= 0x47, .acc_ref[10]		= 0x47,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x90, .cti_delay[10]		= 0x90,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xab, .sub_saturation[2] 	= 0xb0, .sub_saturation[3] 	= 0xb0, .sub_saturation[4] 	= 0xa4, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x90, .sub_saturation[10] = 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90, .c_option[6] 		= 0x90, .c_option[7] 		= 0x90, .c_option[8] 		= 0x90, .c_option[9] 		= 0x90, .c_option[10] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0xa0, .h_delay_a[1] = 0xa0, .h_delay_a[2] = 0x9e, .h_delay_a[3] = 0x9b, .h_delay_a[4] = 0x99, .h_delay_a[5] = 0x99, .h_delay_a[6] = 0x9b, .h_delay_a[7] = 0x98, .h_delay_a[8] = 0x98, .h_delay_a[9] = 0x98, .h_delay_a[10] = 0x98,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x01, .comb_mode[1]	= 0x01, .comb_mode[2]   = 0x01, .comb_mode[3]   = 0x01, .comb_mode[4]  	= 0x01, .comb_mode[5]   = 0x01, .comb_mode[6]	= 0x01, .comb_mode[7]   = 0x01, .comb_mode[8]   = 0x01, .comb_mode[9]  	= 0x01, .comb_mode[10]   = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x00, .mem_path[1]	= 0x00, .mem_path[2]	= 0x00, .mem_path[3]	= 0x00, .mem_path[4]	= 0x00, .mem_path[5]	= 0x00, .mem_path[6]	= 0x00, .mem_path[7]	= 0x00, .mem_path[8]	= 0x00, .mem_path[9]	= 0x00, .mem_path[10]	= 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x06, .format_set1[1] = 0x06, .format_set1[2] = 0x06, .format_set1[3] = 0x06, .format_set1[4] = 0x06, .format_set1[5] = 0x06, .format_set1[6] = 0x06, .format_set1[7] = 0x06, .format_set1[8] = 0x06, .format_set1[9] = 0x06, .format_set1[10] = 0x06,
			/*B0 0x85*/	.format_set2[0] = 0x00, .format_set2[1] = 0x00, .format_set2[2] = 0x00, .format_set2[3] = 0x00, .format_set2[4] = 0x00, .format_set2[5] = 0x00, .format_set2[6] = 0x00, .format_set2[7] = 0x00, .format_set2[8] = 0x00, .format_set2[9] = 0x00, .format_set2[10] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x21, .v_delay[1]     = 0x21, .v_delay[2]     = 0x21, .v_delay[3]     = 0x21, .v_delay[4]     = 0x21, .v_delay[5]     = 0x21, .v_delay[6]     = 0x21, .v_delay[7]     = 0x21, .v_delay[8]     = 0x21, .v_delay[9]     = 0x21, .v_delay[10]     = 0x21,
		},
	},

	[ CVI_HD_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x8c, .contrast[2] 		= 0x8a, .contrast[3] 		= 0x87, .contrast[4] 		= 0x85, .contrast[5] 		= 0x84, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x79, .contrast[9] 		= 0x72, .contrast[10] 		= 0x72,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x86, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x86, .black_level[5] 	= 0x88, .black_level[6] 	= 0x8a, .black_level[7] 	= 0x90, .black_level[8] 	= 0x93, .black_level[9] 	= 0x96, .black_level[10] 	= 0x96,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xa0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x60, .h_delay_a[1] = 0x60, .h_delay_a[2] = 0x5e, .h_delay_a[3] = 0x5b, .h_delay_a[4] = 0x59, .h_delay_a[5] = 0x59, .h_delay_a[6] = 0x5b, .h_delay_a[7] = 0x58, .h_delay_a[8] = 0x58, .h_delay_a[9] = 0x58, .h_delay_a[10] = 0x58,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x01, .h_scaler1[1]   = 0x01, .h_scaler1[2]   = 0x01, .h_scaler1[3]   = 0x01, .h_scaler1[4]   = 0x01, .h_scaler1[5]   = 0x01, .h_scaler1[6]   = 0x01, .h_scaler1[7]   = 0x01, .h_scaler1[8]   = 0x01, .h_scaler1[9]   = 0x01, .h_scaler1[10]   = 0x01,
			/*B9 0x97*/	.h_scaler2[0]  = 0x29, .h_scaler2[1]   = 0x29, .h_scaler2[2]   = 0x29, .h_scaler2[3]   = 0x29, .h_scaler2[4]   = 0x29, .h_scaler2[5]   = 0x29, .h_scaler2[6]   = 0x29, .h_scaler2[7]   = 0x29, .h_scaler2[8]   = 0x29, .h_scaler2[9]   = 0x29, .h_scaler2[10]   = 0x29,
			/*B9 0x98*/	.h_scaler3[0]  = 0xc0, .h_scaler3[1]   = 0xc0, .h_scaler3[2]   = 0xc0, .h_scaler3[3]   = 0xc0, .h_scaler3[4]   = 0xc0, .h_scaler3[5]   = 0xc0, .h_scaler3[6]   = 0xc0, .h_scaler3[7]   = 0xc0, .h_scaler3[8]   = 0xc0, .h_scaler3[9]   = 0xc0, .h_scaler3[10]   = 0xc0,
			/*B9 0x99*/	.h_scaler4[0]  = 0xc1, .h_scaler4[1]   = 0xc1, .h_scaler4[2]   = 0xc1, .h_scaler4[3]   = 0xc1, .h_scaler4[4]   = 0xc1, .h_scaler4[5]   = 0xc1, .h_scaler4[6]   = 0xc1, .h_scaler4[7]   = 0xc1, .h_scaler4[8]   = 0xc1, .h_scaler4[9]   = 0xc1, .h_scaler4[10]   = 0xc1,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x07, .format_set1[1] = 0x07, .format_set1[2] = 0x07, .format_set1[3] = 0x07, .format_set1[4] = 0x07, .format_set1[5] = 0x07, .format_set1[6] = 0x07, .format_set1[7] = 0x07, .format_set1[8] = 0x07, .format_set1[9] = 0x07, .format_set1[10] = 0x07,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02, .format_set2[6] = 0x02, .format_set2[7] = 0x02, .format_set2[8] = 0x02, .format_set2[9] = 0x02, .format_set2[10] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x21, .v_delay[1]     = 0x21, .v_delay[2]     = 0x21, .v_delay[3]     = 0x21, .v_delay[4]     = 0x21, .v_delay[5]     = 0x21, .v_delay[6]     = 0x21, .v_delay[7]     = 0x21, .v_delay[8]     = 0x21, .v_delay[9]     = 0x21, .v_delay[10]     = 0x21,
		},
	},
	[ CVI_HD_30P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x8c, .contrast[2] 		= 0x8a, .contrast[3] 		= 0x87, .contrast[4] 		= 0x85, .contrast[5] 		= 0x84, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x79, .contrast[9] 		= 0x72, .contrast[10] 		= 0x72,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x86, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x86, .black_level[5] 	= 0x88, .black_level[6] 	= 0x8a, .black_level[7] 	= 0x90, .black_level[8] 	= 0x93, .black_level[9] 	= 0x96, .black_level[10] 	= 0x96,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0xa0, .c_option[5] 		= 0xa0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x60, .h_delay_a[1] = 0x60, .h_delay_a[2] = 0x5e, .h_delay_a[3] = 0x5b, .h_delay_a[4] = 0x59, .h_delay_a[5] = 0x59, .h_delay_a[6] = 0x5b, .h_delay_a[7] = 0x58, .h_delay_a[8] = 0x58, .h_delay_a[9] = 0x58, .h_delay_a[10] = 0x58,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x01, .h_scaler1[1]   = 0x01, .h_scaler1[2]   = 0x01, .h_scaler1[3]   = 0x01, .h_scaler1[4]   = 0x01, .h_scaler1[5]   = 0x01, .h_scaler1[6]   = 0x01, .h_scaler1[7]   = 0x01, .h_scaler1[8]   = 0x01, .h_scaler1[9]   = 0x01, .h_scaler1[10]   = 0x01,
			/*B9 0x97*/	.h_scaler2[0]  = 0x29, .h_scaler2[1]   = 0x29, .h_scaler2[2]   = 0x29, .h_scaler2[3]   = 0x29, .h_scaler2[4]   = 0x29, .h_scaler2[5]   = 0x29, .h_scaler2[6]   = 0x29, .h_scaler2[7]   = 0x29, .h_scaler2[8]   = 0x29, .h_scaler2[9]   = 0x29, .h_scaler2[10]   = 0x29,
			/*B9 0x98*/	.h_scaler3[0]  = 0xc0, .h_scaler3[1]   = 0xc0, .h_scaler3[2]   = 0xc0, .h_scaler3[3]   = 0xc0, .h_scaler3[4]   = 0xc0, .h_scaler3[5]   = 0xc0, .h_scaler3[6]   = 0xc0, .h_scaler3[7]   = 0xc0, .h_scaler3[8]   = 0xc0, .h_scaler3[9]   = 0xc0, .h_scaler3[10]   = 0xc0,
			/*B9 0x99*/	.h_scaler4[0]  = 0xc1, .h_scaler4[1]   = 0xc1, .h_scaler4[2]   = 0xc1, .h_scaler4[3]   = 0xc1, .h_scaler4[4]   = 0xc1, .h_scaler4[5]   = 0xc1, .h_scaler4[6]   = 0xc1, .h_scaler4[7]   = 0xc1, .h_scaler4[8]   = 0xc1, .h_scaler4[9]   = 0xc1, .h_scaler4[10]   = 0xc1,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x06, .format_set1[1] = 0x06, .format_set1[2] = 0x06, .format_set1[3] = 0x06, .format_set1[4] = 0x06, .format_set1[5] = 0x06, .format_set1[6] = 0x06, .format_set1[7] = 0x06, .format_set1[8] = 0x06, .format_set1[9] = 0x06, .format_set1[10] = 0x06,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02, .format_set2[6] = 0x02, .format_set2[7] = 0x02, .format_set2[8] = 0x02, .format_set2[9] = 0x02, .format_set2[10] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x21, .v_delay[1]     = 0x21, .v_delay[2]     = 0x21, .v_delay[3]     = 0x21, .v_delay[4]     = 0x21, .v_delay[5]     = 0x21, .v_delay[6]     = 0x21, .v_delay[7]     = 0x21, .v_delay[8]     = 0x21, .v_delay[9]     = 0x21, .v_delay[10]     = 0x21,
		},
	},

	[ CVI_HD_25P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x63, .eq_band_sel[4] = 0x53, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x57, .eq_band_sel[7] = 0x17, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7d, .eq_gain_sel[5] = 0x7d, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x8c, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94, .deq_a_sel[6] 	= 0x95, .deq_a_sel[7] 	= 0x91, .deq_a_sel[8] 	= 0x92, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x80, .contrast[2] 		= 0x80, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x72, .contrast[10] 		= 0x72,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x00, .h_peaking[8] 		= 0x00, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xa2, .c_filter[7] 		= 0xa2, .c_filter[8] 		= 0xa2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x08, .u_offset[1] 		= 0x08, .u_offset[2] 		= 0x08, .u_offset[3] 		= 0x08, .u_offset[4] 		= 0x08, .u_offset[5] 		= 0x08, .u_offset[6] 		= 0x08, .u_offset[7] 		= 0x08, .u_offset[8] 		= 0x08, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8, .v_offset[6] 		= 0xf8, .v_offset[7] 		= 0xf8, .v_offset[8] 		= 0xf8, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x7c, .black_level[1] 	= 0x7c, .black_level[2] 	= 0x7c, .black_level[3] 	= 0x7c, .black_level[4] 	= 0x7c, .black_level[5] 	= 0x7c, .black_level[6] 	= 0x7c, .black_level[7] 	= 0x7c, .black_level[8] 	= 0x7c, .black_level[9] 	= 0x96, .black_level[10] 	= 0x96,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x47, .acc_ref[8]			= 0x37, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa0, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xa0, .sub_saturation[4] 	= 0xa0, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x6b, .h_delay_a[1] = 0x60, .h_delay_a[2] = 0x6a, .h_delay_a[3] = 0x69, .h_delay_a[4] = 0x69, .h_delay_a[5] = 0x6a, .h_delay_a[6] = 0x7a, .h_delay_a[7] = 0x7a, .h_delay_a[8] = 0x7a, .h_delay_a[9] = 0x7a, .h_delay_a[10] = 0x7a,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x29, .h_scaler2[1]   = 0x29, .h_scaler2[2]   = 0x29, .h_scaler2[3]   = 0x29, .h_scaler2[4]   = 0x29, .h_scaler2[5]   = 0x29, .h_scaler2[6]   = 0x29, .h_scaler2[7]   = 0x29, .h_scaler2[8]   = 0x29, .h_scaler2[9]   = 0x29, .h_scaler2[10]   = 0x29,
			/*B9 0x98*/	.h_scaler3[0]  = 0xdc, .h_scaler3[1]   = 0xdc, .h_scaler3[2]   = 0xdc, .h_scaler3[3]   = 0xdc, .h_scaler3[4]   = 0xdc, .h_scaler3[5]   = 0xdc, .h_scaler3[6]   = 0xdc, .h_scaler3[7]   = 0xdc, .h_scaler3[8]   = 0xdc, .h_scaler3[9]   = 0xdc, .h_scaler3[10]   = 0xdc,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80, .h_scaler9[6]   = 0x80, .h_scaler9[7]   = 0x80, .h_scaler9[8]   = 0x80, .h_scaler9[9]   = 0x80, .h_scaler9[10]   = 0x80,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0b, .format_set1[1] = 0x0b, .format_set1[2] = 0x0b, .format_set1[3] = 0x0b, .format_set1[4] = 0x0b, .format_set1[5] = 0x0b, .format_set1[6] = 0x0b, .format_set1[7] = 0x0b, .format_set1[8] = 0x0b, .format_set1[9] = 0x0b, .format_set1[10] = 0x0b,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02, .format_set2[6] = 0x02, .format_set2[7] = 0x02, .format_set2[8] = 0x02, .format_set2[9] = 0x02, .format_set2[10] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},
	[ CVI_HD_30P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x63, .eq_band_sel[4] = 0x53, .eq_band_sel[5] = 0x57, .eq_band_sel[6] = 0x57, .eq_band_sel[7] = 0x17, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7d, .eq_gain_sel[5] = 0x7d, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x8c, .deq_a_sel[3] 	= 0x8d, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x94, .deq_a_sel[6] 	= 0x95, .deq_a_sel[7] 	= 0x91, .deq_a_sel[8] 	= 0x92, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x80, .contrast[1] 		= 0x80, .contrast[2] 		= 0x80, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x72, .contrast[10] 		= 0x72,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x00, .h_peaking[8] 		= 0x00, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xa2, .c_filter[7] 		= 0xa2, .c_filter[8] 		= 0xa2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x00, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x08, .u_offset[1] 		= 0x08, .u_offset[2] 		= 0x08, .u_offset[3] 		= 0x08, .u_offset[4] 		= 0x08, .u_offset[5] 		= 0x08, .u_offset[6] 		= 0x08, .u_offset[7] 		= 0x08, .u_offset[8] 		= 0x08, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf8, .v_offset[5] 		= 0xf8, .v_offset[6] 		= 0xf8, .v_offset[7] 		= 0xf8, .v_offset[8] 		= 0xf8, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x7c, .black_level[1] 	= 0x7c, .black_level[2] 	= 0x7c, .black_level[3] 	= 0x7c, .black_level[4] 	= 0x7c, .black_level[5] 	= 0x7c, .black_level[6] 	= 0x7c, .black_level[7] 	= 0x7c, .black_level[8] 	= 0x7c, .black_level[9] 	= 0x96, .black_level[10] 	= 0x96,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x47, .acc_ref[8]			= 0x37, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa0, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0xa0, .sub_saturation[4] 	= 0xa0, .sub_saturation[5] 	= 0xa0, .sub_saturation[6] 	= 0xa0, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0x90, .c_option[5] 		= 0xa0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80, .h_delay_a[6] = 0x7a, .h_delay_a[7] = 0x7a, .h_delay_a[8] = 0x7a, .h_delay_a[9] = 0x7a, .h_delay_a[10] = 0x7a,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x19, .h_scaler2[1]   = 0x19, .h_scaler2[2]   = 0x19, .h_scaler2[3]   = 0x19, .h_scaler2[4]   = 0x19, .h_scaler2[5]   = 0x19, .h_scaler2[6]   = 0x19, .h_scaler2[7]   = 0x19, .h_scaler2[8]   = 0x19, .h_scaler2[9]   = 0x19, .h_scaler2[10]   = 0x19,
			/*B9 0x98*/	.h_scaler3[0]  = 0x50, .h_scaler3[1]   = 0x50, .h_scaler3[2]   = 0x50, .h_scaler3[3]   = 0x50, .h_scaler3[4]   = 0x50, .h_scaler3[5]   = 0x50, .h_scaler3[6]   = 0x50, .h_scaler3[7]   = 0x50, .h_scaler3[8]   = 0x50, .h_scaler3[9]   = 0x50, .h_scaler3[10]   = 0x50,
			/*B9 0x99*/	.h_scaler4[0]  = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01, .h_scaler4[6]   = 0x01, .h_scaler4[7]   = 0x01, .h_scaler4[8]   = 0x01, .h_scaler4[9]   = 0x01, .h_scaler4[10]   = 0x01,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80, .h_scaler9[6]   = 0x80, .h_scaler9[7]   = 0x80, .h_scaler9[8]   = 0x80, .h_scaler9[9]   = 0x80, .h_scaler9[10]   = 0x80,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0a, .format_set1[1] = 0x0a, .format_set1[2] = 0x0a, .format_set1[3] = 0x0a, .format_set1[4] = 0x0a, .format_set1[5] = 0x0a, .format_set1[6] = 0x0a, .format_set1[7] = 0x0a, .format_set1[8] = 0x0a, .format_set1[9] = 0x0a, .format_set1[10] = 0x0a,
			/*B0 0x85*/	.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02, .format_set2[6] = 0x02, .format_set2[7] = 0x02, .format_set2[8] = 0x02, .format_set2[9] = 0x02, .format_set2[10] = 0x02,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},
	[ CVI_HD_50P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7f, .eq_gain_sel[2] = 0x6f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x95, .deq_a_sel[5] 	= 0x93,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x98, .contrast[1] 		= 0x98, .contrast[2] 		= 0x98, .contrast[3] 		= 0x98, .contrast[4] 		= 0x98, .contrast[5] 		= 0x98,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x03, .hue[1] 			= 0x03, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x0c, .u_gain[1] 			= 0x0c, .u_gain[2] 			= 0x0c, .u_gain[3] 			= 0x0c, .u_gain[4] 			= 0x0c, .u_gain[5] 			= 0x0c,
			.v_gain[0] 			= 0x1a, .v_gain[1] 			= 0x1a, .v_gain[2] 			= 0x1a, .v_gain[3] 			= 0x1a, .v_gain[4] 			= 0x1a, .v_gain[5] 			= 0x1a,
			.u_offset[0] 		= 0xfa, .u_offset[1] 		= 0xfa, .u_offset[2] 		= 0xfa, .u_offset[3] 		= 0xfa, .u_offset[4] 		= 0xfa, .u_offset[5] 		= 0xfa,
			.v_offset[0] 		= 0xfa, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfa, .v_offset[3] 		= 0xfa, .v_offset[4] 		= 0xfa, .v_offset[5] 		= 0xfa,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x7a, .black_level[2] 	= 0x88, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x84,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0x90, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0xc0, .c_option[1] 		= 0xc0, .c_option[2] 		= 0xc0, .c_option[3] 		= 0xc0, .c_option[4] 		= 0xc0, .c_option[5] 		= 0xc0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]  = 0x29, .h_scaler2[1]   = 0x29, .h_scaler2[2]   = 0x29, .h_scaler2[3]   = 0x29, .h_scaler2[4]   = 0x29, .h_scaler2[5]   = 0x29,
			.h_scaler3[0]  = 0xe0, .h_scaler3[1]   = 0xe0, .h_scaler3[2]   = 0xe0, .h_scaler3[3]   = 0xe0, .h_scaler3[4]   = 0xe0, .h_scaler3[5]   = 0xe0,
			.h_scaler4[0]  = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01,
			.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80,

			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x05, .format_set1[1] = 0x05, .format_set1[2] = 0x05, .format_set1[3] = 0x05, .format_set1[4] = 0x05, .format_set1[5] = 0x05,
			.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
	[ CVI_HD_60P ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x47,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7f, .eq_gain_sel[2] = 0x6f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x95, .deq_a_sel[5] 	= 0x93,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x98, .contrast[1] 		= 0x98, .contrast[2] 		= 0x98, .contrast[3] 		= 0x98, .contrast[4] 		= 0x98, .contrast[5] 		= 0x98,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00,
			.c_filter[0]		= 0x92, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2,

			.hue[0] 			= 0x03, .hue[1] 			= 0x03, .hue[2] 			= 0x04, .hue[3] 			= 0x04, .hue[4] 			= 0x04, .hue[5] 			= 0x04,
			.u_gain[0] 			= 0x0c, .u_gain[1] 			= 0x0c, .u_gain[2] 			= 0x0c, .u_gain[3] 			= 0x0c, .u_gain[4] 			= 0x0c, .u_gain[5] 			= 0x0c,
			.v_gain[0] 			= 0x1a, .v_gain[1] 			= 0x1a, .v_gain[2] 			= 0x1a, .v_gain[3] 			= 0x1a, .v_gain[4] 			= 0x1a, .v_gain[5] 			= 0x1a,
			.u_offset[0] 		= 0xfa, .u_offset[1] 		= 0xfa, .u_offset[2] 		= 0xfa, .u_offset[3] 		= 0xfa, .u_offset[4] 		= 0xfa, .u_offset[5] 		= 0xfa,
			.v_offset[0] 		= 0xfa, .v_offset[1] 		= 0xfa, .v_offset[2] 		= 0xfa, .v_offset[3] 		= 0xfa, .v_offset[4] 		= 0xfa, .v_offset[5] 		= 0xfa,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x7a, .black_level[2] 	= 0x88, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x84,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x47, .acc_ref[5]			= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xa0, .sub_saturation[2] 	= 0xa0, .sub_saturation[3] 	= 0x90, .sub_saturation[4] 	= 0x90, .sub_saturation[5] 	= 0x90,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0xc0, .c_option[1] 		= 0xc0, .c_option[2] 		= 0xc0, .c_option[3] 		= 0xc0, .c_option[4] 		= 0xc0, .c_option[5] 		= 0xc0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,

		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]   = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			.h_scaler2[0]   = 0x19, .h_scaler2[1]   = 0x19, .h_scaler2[2]   = 0x19, .h_scaler2[3]   = 0x19, .h_scaler2[4]   = 0x19, .h_scaler2[5]   = 0x19,
			.h_scaler3[0]   = 0x28, .h_scaler3[1]   = 0x28, .h_scaler3[2]   = 0x28, .h_scaler3[3]   = 0x28, .h_scaler3[4]   = 0x28, .h_scaler3[5]   = 0x28,
			.h_scaler4[0]   = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x01, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01,
			.h_scaler5[0]   = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			.h_scaler6[0]   = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			.h_scaler7[0]   = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			.h_scaler8[0]   = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			.h_scaler9[0]   = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80,
			.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,
			.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05,
			.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x04, .format_set1[1] = 0x04, .format_set1[2] = 0x04, .format_set1[3] = 0x04, .format_set1[4] = 0x04, .format_set1[5] = 0x04,
			.format_set2[0] = 0x02, .format_set2[1] = 0x02, .format_set2[2] = 0x02, .format_set2[3] = 0x02, .format_set2[4] = 0x02, .format_set2[5] = 0x02,

			.v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ TVI_HD_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x86, .contrast[2] 		= 0x84, .contrast[3] 		= 0x81, .contrast[4] 		= 0x7f, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x7a, .contrast[7] 		= 0x7a, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0xe0, .u_gain[1] 			= 0xe0, .u_gain[2] 			= 0xe0, .u_gain[3] 			= 0xe0, .u_gain[4] 			= 0xe0, .u_gain[5] 			= 0xe0, .u_gain[6] 			= 0xe0, .u_gain[7] 			= 0xe0, .u_gain[8] 			= 0xe0, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0xff, .u_offset[1] 		= 0xff, .u_offset[2] 		= 0xff, .u_offset[3] 		= 0xff, .u_offset[4] 		= 0xff, .u_offset[5] 		= 0xff, .u_offset[6] 		= 0xff, .u_offset[7] 		= 0xff, .u_offset[8] 		= 0xff, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x88, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x88, .black_level[5] 	= 0x8a, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x92, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x7e, .h_delay_a[2] = 0x7d, .h_delay_a[3] = 0x7c, .h_delay_a[4] = 0x5c, .h_delay_a[5] = 0x7d, .h_delay_a[6] = 0x7d, .h_delay_a[7] = 0x7d, .h_delay_a[8] = 0x7d, .h_delay_a[9] = 0x7d, .h_delay_a[10] = 0x7d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x20, .h_delay_c[1] = 0x20, .h_delay_c[2] = 0x20, .h_delay_c[3] = 0x20, .h_delay_c[4] = 0x20, .h_delay_c[5] = 0x20, .h_delay_c[6] = 0x20, .h_delay_c[7] = 0x20, .h_delay_c[8] = 0x20, .h_delay_c[9] = 0x20, .h_delay_c[10] = 0x20,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05, .clk_adc[6] = 0x05, .clk_adc[7] = 0x05, .clk_adc[8] = 0x05, .clk_adc[9] = 0x05, .clk_adc[10] = 0x05,
			.clk_dec[0] = 0x04, .clk_dec[1] = 0x04, .clk_dec[2] = 0x04, .clk_dec[3] = 0x04, .clk_dec[4] = 0x04, .clk_dec[5] = 0x04, .clk_dec[6] = 0x04, .clk_dec[7] = 0x04, .clk_dec[8] = 0x04, .clk_dec[9] = 0x04, .clk_dec[10] = 0x04,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x01, .h_scaler1[1]   = 0x01, .h_scaler1[2]   = 0x01, .h_scaler1[3]   = 0x01, .h_scaler1[4]   = 0x01, .h_scaler1[5]   = 0x01, .h_scaler1[6]   = 0x01, .h_scaler1[7]   = 0x01, .h_scaler1[8]   = 0x01, .h_scaler1[9]   = 0x01, .h_scaler1[10]   = 0x01,
			/*B9 0x97*/	.h_scaler2[0]  = 0x59, .h_scaler2[1]   = 0x59, .h_scaler2[2]   = 0x59, .h_scaler2[3]   = 0x59, .h_scaler2[4]   = 0x59, .h_scaler2[5]   = 0x59, .h_scaler2[6]   = 0x59, .h_scaler2[7]   = 0x59, .h_scaler2[8]   = 0x59, .h_scaler2[9]   = 0x59, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0xc0, .h_scaler3[1]   = 0xc0, .h_scaler3[2]   = 0xc0, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0xc0, .h_scaler3[5]   = 0xc0, .h_scaler3[6]   = 0xc0, .h_scaler3[7]   = 0xc0, .h_scaler3[8]   = 0xc0, .h_scaler3[9]   = 0xc0, .h_scaler3[10]   = 0xc0,
			/*B9 0x99*/	.h_scaler4[0]  = 0x01, .h_scaler4[1]   = 0x01, .h_scaler4[2]   = 0x01, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x01, .h_scaler4[5]   = 0x01, .h_scaler4[6]   = 0x01, .h_scaler4[7]   = 0x01, .h_scaler4[8]   = 0x01, .h_scaler4[9]   = 0x01, .h_scaler4[10]   = 0x01,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x07, .format_set1[1] = 0x07, .format_set1[2] = 0x07, .format_set1[3] = 0x07, .format_set1[4] = 0x07, .format_set1[5] = 0x07, .format_set1[6] = 0x07, .format_set1[7] = 0x07, .format_set1[8] = 0x07, .format_set1[9] = 0x07, .format_set1[10] = 0x07,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x21, .v_delay[1]     = 0x21, .v_delay[2]     = 0x21, .v_delay[3]     = 0x21, .v_delay[4]     = 0x21, .v_delay[5]     = 0x21, .v_delay[6]     = 0x21, .v_delay[7]     = 0x21, .v_delay[8]     = 0x21, .v_delay[9]     = 0x21, .v_delay[10]     = 0x21,
		},
	},
	[ TVI_HD_30P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x86, .contrast[2] 		= 0x84, .contrast[3] 		= 0x81, .contrast[4] 		= 0x7f, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x7a, .contrast[7] 		= 0x7a, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0xe0, .u_gain[1] 			= 0xe0, .u_gain[2] 			= 0xe0, .u_gain[3] 			= 0xe0, .u_gain[4] 			= 0xe0, .u_gain[5] 			= 0xe0, .u_gain[6] 			= 0xe0, .u_gain[7] 			= 0xe0, .u_gain[8] 			= 0xe0, .u_gain[9] 			= 0xe0, .u_gain[10] 		= 0xe0,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0xff, .u_offset[1] 		= 0xff, .u_offset[2] 		= 0xff, .u_offset[3] 		= 0xff, .u_offset[4] 		= 0xff, .u_offset[5] 		= 0xff, .u_offset[6] 		= 0xff, .u_offset[7] 		= 0xff, .u_offset[8] 		= 0xff, .u_offset[9] 		= 0xff, .u_offset[10] 		= 0xff,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x88, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x88, .black_level[5] 	= 0x8a, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x92, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x20, .h_delay_a[1] = 0x1e, .h_delay_a[2] = 0x1d, .h_delay_a[3] = 0x1c, .h_delay_a[4] = 0x1c, .h_delay_a[5] = 0x1d, .h_delay_a[6] = 0x1d, .h_delay_a[7] = 0x1d, .h_delay_a[8] = 0x1d, .h_delay_a[9] = 0x1d, .h_delay_a[10] = 0x1d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x10, .h_delay_c[1] = 0x10, .h_delay_c[2] = 0x10, .h_delay_c[3] = 0x10, .h_delay_c[4] = 0x10, .h_delay_c[5] = 0x10, .h_delay_c[6] = 0x10, .h_delay_c[7] = 0x10, .h_delay_c[8] = 0x10, .h_delay_c[9] = 0x10, .h_delay_c[10] = 0x10,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05, .clk_adc[6] = 0x05, .clk_adc[7] = 0x05, .clk_adc[8] = 0x05, .clk_adc[9] = 0x05, .clk_adc[10] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44, .clk_dec[6] = 0x44, .clk_dec[7] = 0x44, .clk_dec[8] = 0x44, .clk_dec[9] = 0x44, .clk_dec[10] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x01, .h_scaler1[1]   = 0x01, .h_scaler1[2]   = 0x01, .h_scaler1[3]   = 0x01, .h_scaler1[4]   = 0x01, .h_scaler1[5]   = 0x01, .h_scaler1[6]   = 0x01, .h_scaler1[7]   = 0x01, .h_scaler1[8]   = 0x01, .h_scaler1[9]   = 0x01, .h_scaler1[10]   = 0x01,
			/*B9 0x97*/	.h_scaler2[0]  = 0x59, .h_scaler2[1]   = 0x59, .h_scaler2[2]   = 0x59, .h_scaler2[3]   = 0x59, .h_scaler2[4]   = 0x59, .h_scaler2[5]   = 0x59, .h_scaler2[6]   = 0x59, .h_scaler2[7]   = 0x59, .h_scaler2[8]   = 0x59, .h_scaler2[9]   = 0x59, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0xff, .h_scaler3[1]   = 0x30, .h_scaler3[2]   = 0x30, .h_scaler3[3]   = 0x30, .h_scaler3[4]   = 0x30, .h_scaler3[5]   = 0x30, .h_scaler3[6]   = 0x30, .h_scaler3[7]   = 0x30, .h_scaler3[8]   = 0x30, .h_scaler3[9]   = 0x30, .h_scaler3[10]   = 0x30,
			/*B9 0x99*/	.h_scaler4[0]  = 0x001, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x06, .format_set1[1] = 0x06, .format_set1[2] = 0x06, .format_set1[3] = 0x06, .format_set1[4] = 0x06, .format_set1[5] = 0x06, .format_set1[6] = 0x06, .format_set1[7] = 0x06, .format_set1[8] = 0x06, .format_set1[9] = 0x06, .format_set1[10] = 0x06,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20, .v_delay[6]     = 0x20, .v_delay[7]     = 0x20, .v_delay[8]     = 0x20, .v_delay[9]     = 0x20, .v_delay[10]     = 0x20,
		},
	},

	[ TVI_HD_B_25P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x86, .contrast[2] 		= 0x84, .contrast[3] 		= 0x81, .contrast[4] 		= 0x7f, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x7a, .contrast[7] 		= 0x7a, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0xe0, .u_gain[1] 			= 0x00, .u_gain[2] 			= 0x00, .u_gain[3] 			= 0x00, .u_gain[4] 			= 0x00, .u_gain[5] 			= 0x00, .u_gain[6] 			= 0x00, .u_gain[7] 			= 0x00, .u_gain[8] 			= 0x00, .u_gain[9] 			= 0x00, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0xff, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00, .u_offset[6] 		= 0x00, .u_offset[7] 		= 0x00, .u_offset[8] 		= 0x00, .u_offset[9] 		= 0x00, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x88, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x88, .black_level[5] 	= 0x8a, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x92, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x82, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x7f, .h_delay_a[3] = 0x7e, .h_delay_a[4] = 0x7e, .h_delay_a[5] = 0x7f, .h_delay_a[6] = 0x7f, .h_delay_a[7] = 0x7f, .h_delay_a[8] = 0x7f, .h_delay_a[9] = 0x7f, .h_delay_a[10] = 0x7f,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10, .h_delay_b[6] = 0x10, .h_delay_b[7] = 0x10, .h_delay_b[8] = 0x10, .h_delay_b[9] = 0x10, .h_delay_b[10] = 0x10,
			.h_delay_c[0] = 0x01, .h_delay_c[1] = 0x01, .h_delay_c[2] = 0x01, .h_delay_c[3] = 0x01, .h_delay_c[4] = 0x01, .h_delay_c[5] = 0x01, .h_delay_c[6] = 0x01, .h_delay_c[7] = 0x01, .h_delay_c[8] = 0x01, .h_delay_c[9] = 0x01, .h_delay_c[10] = 0x01,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x07, .format_set1[1] = 0x07, .format_set1[2] = 0x07, .format_set1[3] = 0x07, .format_set1[4] = 0x07, .format_set1[5] = 0x07, .format_set1[6] = 0x07, .format_set1[7] = 0x07, .format_set1[8] = 0x07, .format_set1[9] = 0x07, .format_set1[10] = 0x07,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03, .format_set2[6] = 0x03, .format_set2[7] = 0x03, .format_set2[8] = 0x03, .format_set2[9] = 0x03, .format_set2[10] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20, .v_delay[6]     = 0x20, .v_delay[7]     = 0x20, .v_delay[8]     = 0x20, .v_delay[9]     = 0x20, .v_delay[10]     = 0x20,
		},
	},
	[ TVI_HD_B_30P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x57, .eq_band_sel[3] = 0x57, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x37, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x78, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x93, .deq_a_sel[6] 	= 0x93, .deq_a_sel[7] 	= 0x93, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x86, .contrast[2] 		= 0x84, .contrast[3] 		= 0x81, .contrast[4] 		= 0x7f, .contrast[5] 		= 0x7e, .contrast[6] 		= 0x7a, .contrast[7] 		= 0x7a, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x00, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x00, .h_peaking[7] 		= 0x3f, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x92, .c_filter[2]	 	= 0x92, .c_filter[3] 		= 0xa2, .c_filter[4] 		= 0xa2, .c_filter[5] 		= 0xa2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00, .hue[6] 			= 0x00, .hue[7] 			= 0x00, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0xe0, .u_gain[1] 			= 0xe0, .u_gain[2] 			= 0xe0, .u_gain[3] 			= 0xe0, .u_gain[4] 			= 0xe0, .u_gain[5] 			= 0xe0, .u_gain[6] 			= 0xe0, .u_gain[7] 			= 0xe0, .u_gain[8] 			= 0xe0, .u_gain[9] 			= 0xe0, .u_gain[10] 		= 0xe0,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0x00, .v_gain[3] 			= 0x00, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0xff, .u_offset[1] 		= 0xff, .u_offset[2] 		= 0xff, .u_offset[3] 		= 0xff, .u_offset[4] 		= 0xff, .u_offset[5] 		= 0xff, .u_offset[6] 		= 0xff, .u_offset[7] 		= 0xff, .u_offset[8] 		= 0xff, .u_offset[9] 		= 0xff, .u_offset[10] 		= 0xff,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00, .v_offset[6] 		= 0x00, .v_offset[7] 		= 0x00, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x88, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x88, .black_level[5] 	= 0x8a, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x92, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x57, .acc_ref[7]			= 0x37, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xa8, .sub_saturation[1] 	= 0xaa, .sub_saturation[2] 	= 0xaa, .sub_saturation[3] 	= 0xac, .sub_saturation[4] 	= 0xa8, .sub_saturation[5] 	= 0xa6, .sub_saturation[6] 	= 0x98, .sub_saturation[7] 	= 0xa0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x7e, .h_delay_a[2] = 0x7d, .h_delay_a[3] = 0x7c, .h_delay_a[4] = 0x7c, .h_delay_a[5] = 0x7d, .h_delay_a[6] = 0x7d, .h_delay_a[7] = 0x7d, .h_delay_a[8] = 0x7d, .h_delay_a[9] = 0x7d, .h_delay_a[10] = 0x7d,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10, .h_delay_b[6] = 0x10, .h_delay_b[7] = 0x10, .h_delay_b[8] = 0x10, .h_delay_b[9] = 0x10, .h_delay_b[10] = 0x10,
			.h_delay_c[0] = 0x02, .h_delay_c[1] = 0x02, .h_delay_c[2] = 0x02, .h_delay_c[3] = 0x02, .h_delay_c[4] = 0x02, .h_delay_c[5] = 0x02, .h_delay_c[6] = 0x02, .h_delay_c[7] = 0x02, .h_delay_c[8] = 0x02, .h_delay_c[9] = 0x02, .h_delay_c[10] = 0x02,
			.y_delay[0]   = 0x05, .y_delay[1]   = 0x05, .y_delay[2]   = 0x05, .y_delay[3]   = 0x05, .y_delay[4]   = 0x05, .y_delay[5] =   0x05, .y_delay[6] =   0x05, .y_delay[7] =   0x05, .y_delay[8] =   0x05, .y_delay[9] =   0x05, .y_delay[10] =   0x05,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x01, .h_scaler1[1]   = 0x01, .h_scaler1[2]   = 0x01, .h_scaler1[3]   = 0x01, .h_scaler1[4]   = 0x01, .h_scaler1[5]   = 0x01, .h_scaler1[6]   = 0x01, .h_scaler1[7]   = 0x01, .h_scaler1[8]   = 0x01, .h_scaler1[9]   = 0x01, .h_scaler1[10]   = 0x01,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x06, .format_set1[1] = 0x06, .format_set1[2] = 0x06, .format_set1[3] = 0x06, .format_set1[4] = 0x06, .format_set1[5] = 0x06, .format_set1[6] = 0x06, .format_set1[7] = 0x06, .format_set1[8] = 0x06, .format_set1[9] = 0x06, .format_set1[10] = 0x06,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03, .format_set2[6] = 0x03, .format_set2[7] = 0x03, .format_set2[8] = 0x03, .format_set2[9] = 0x03, .format_set2[10] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20, .v_delay[6]     = 0x20, .v_delay[7]     = 0x20, .v_delay[8]     = 0x20, .v_delay[9]     = 0x20, .v_delay[10]     = 0x20,
		},
	},

	[ TVI_HD_25P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x27, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7b, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x92, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x82, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x98, .contrast[7] 		= 0x98, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x05, .hue[1] 			= 0x05, .hue[2] 			= 0x05, .hue[3] 			= 0x05, .hue[4] 			= 0x05, .hue[5] 			= 0x05, .hue[6] 			= 0x05, .hue[7] 			= 0x05, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70, .u_gain[6] 			= 0x70, .u_gain[7] 			= 0x70, .u_gain[8] 			= 0xe0, .u_gain[9] 			= 0xe0, .u_gain[10] 		= 0xe0,
			.v_gain[0] 			= 0xf0, .v_gain[1] 			= 0xf0, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0xff, .u_offset[9] 		= 0xff, .u_offset[10] 		= 0xff,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf8, .v_offset[6] 		= 0xf8, .v_offset[7] 		= 0xf8, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x88, .black_level[1] 	= 0x8c, .black_level[2] 	= 0x8a, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x86, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x86, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x40, .acc_ref[5]			= 0x40, .acc_ref[6]			= 0x40, .acc_ref[7]			= 0x40, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xe0, .sub_saturation[3] 	= 0xe0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0xd0, .sub_saturation[6] 	= 0xd0, .sub_saturation[7] 	= 0xd0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x5e, .h_delay_a[2] = 0x5d, .h_delay_a[3] = 0x5c, .h_delay_a[4] = 0x5c, .h_delay_a[5] = 0x5d, .h_delay_a[6] = 0x5d, .h_delay_a[7] = 0x5d, .h_delay_a[8] = 0x5d, .h_delay_a[9] = 0x5d, .h_delay_a[10] = 0x5d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05, .clk_adc[6] = 0x05, .clk_adc[7] = 0x05, .clk_adc[8] = 0x05, .clk_adc[9] = 0x05, .clk_adc[10] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44, .clk_dec[6] = 0x44, .clk_dec[7] = 0x44, .clk_dec[8] = 0x44, .clk_dec[9] = 0x44, .clk_dec[10] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x59, .h_scaler2[1]   = 0x59, .h_scaler2[2]   = 0x59, .h_scaler2[3]   = 0x59, .h_scaler2[4]   = 0x59, .h_scaler2[5]   = 0x59, .h_scaler2[6]   = 0x59, .h_scaler2[7]   = 0x59, .h_scaler2[8]   = 0x59, .h_scaler2[9]   = 0x59, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80, .h_scaler9[6]   = 0x80, .h_scaler9[7]   = 0x80, .h_scaler9[8]   = 0x80, .h_scaler9[9]   = 0x80, .h_scaler9[10]   = 0x80,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0b, .format_set1[1] = 0x0b, .format_set1[2] = 0x0b, .format_set1[3] = 0x0b, .format_set1[4] = 0x0b, .format_set1[5] = 0x0b, .format_set1[6] = 0x0b, .format_set1[7] = 0x0b, .format_set1[8] = 0x0b, .format_set1[9] = 0x0b, .format_set1[10] = 0x0b,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},
	[ TVI_HD_30P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x22, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x27, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x17, .eq_band_sel[9] = 0x17, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7b, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x93, .deq_a_sel[3] 	= 0x94, .deq_a_sel[4] 	= 0x92, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x92, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x82, .contrast[1] 		= 0x82, .contrast[2] 		= 0x82, .contrast[3] 		= 0x82, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x98, .contrast[7] 		= 0x98, .contrast[8] 		= 0x72, .contrast[9] 		= 0x6d, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x00, .h_peaking[2] 		= 0x00, .h_peaking[3] 		= 0x00, .h_peaking[4] 		= 0x70, .h_peaking[5] 		= 0x70, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x3f, .h_peaking[9] 		= 0x3f, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x92, .c_filter[4] 		= 0xb2, .c_filter[5] 		= 0xb2, .c_filter[6] 		= 0xb2, .c_filter[7] 		= 0xb2, .c_filter[8] 		= 0xb2, .c_filter[9] 		= 0xb2, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x05, .hue[1] 			= 0x05, .hue[2] 			= 0x05, .hue[3] 			= 0x05, .hue[4] 			= 0x05, .hue[5] 			= 0x05, .hue[6] 			= 0x05, .hue[7] 			= 0x05, .hue[8] 			= 0x00, .hue[9] 			= 0x00, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x70, .u_gain[5] 			= 0x70, .u_gain[6] 			= 0x70, .u_gain[7] 			= 0x70, .u_gain[8] 			= 0xe0, .u_gain[9] 			= 0xe0, .u_gain[10] 		= 0xe0,
			.v_gain[0] 			= 0xf0, .v_gain[1] 			= 0xf0, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0x00, .v_gain[5] 			= 0x00, .v_gain[6] 			= 0x00, .v_gain[7] 			= 0x00, .v_gain[8] 			= 0x00, .v_gain[9] 			= 0x00, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0xff, .u_offset[9] 		= 0xff, .u_offset[10] 		= 0xff,
			.v_offset[0] 		= 0xf8, .v_offset[1] 		= 0xf8, .v_offset[2] 		= 0xf8, .v_offset[3] 		= 0xf8, .v_offset[4] 		= 0xf0, .v_offset[5] 		= 0xf8, .v_offset[6] 		= 0xf8, .v_offset[7] 		= 0xf8, .v_offset[8] 		= 0x00, .v_offset[9] 		= 0x00, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x88, .black_level[1] 	= 0x8c, .black_level[2] 	= 0x8a, .black_level[3] 	= 0x8a, .black_level[4] 	= 0x86, .black_level[5] 	= 0x86, .black_level[6] 	= 0x86, .black_level[7] 	= 0x86, .black_level[8] 	= 0x95, .black_level[9] 	= 0x98, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x40, .acc_ref[5]			= 0x40, .acc_ref[6]			= 0x40, .acc_ref[7]			= 0x40, .acc_ref[8]			= 0x20, .acc_ref[9]			= 0x20, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xe0, .sub_saturation[3] 	= 0xe0, .sub_saturation[4] 	= 0xd0, .sub_saturation[5] 	= 0xd0, .sub_saturation[6] 	= 0xd0, .sub_saturation[7] 	= 0xd0, .sub_saturation[8] 	= 0xa0, .sub_saturation[9] 	= 0x50, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x90, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x7e, .h_delay_a[2] = 0x7d, .h_delay_a[3] = 0x7c, .h_delay_a[4] = 0x7c, .h_delay_a[5] = 0x7d, .h_delay_a[6] = 0x7d, .h_delay_a[7] = 0x7d, .h_delay_a[8] = 0x7d, .h_delay_a[9] = 0x7d, .h_delay_a[10] = 0x7d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05, .clk_adc[6] = 0x05, .clk_adc[7] = 0x05, .clk_adc[8] = 0x05, .clk_adc[9] = 0x05, .clk_adc[10] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44, .clk_dec[6] = 0x44, .clk_dec[7] = 0x44, .clk_dec[8] = 0x44, .clk_dec[9] = 0x44, .clk_dec[10] = 0x44,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x59, .h_scaler2[1]   = 0x59, .h_scaler2[2]   = 0x59, .h_scaler2[3]   = 0x59, .h_scaler2[4]   = 0x59, .h_scaler2[5]   = 0x59, .h_scaler2[6]   = 0x59, .h_scaler2[7]   = 0x59, .h_scaler2[8]   = 0x59, .h_scaler2[9]   = 0x59, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0x20, .h_scaler3[1]   = 0x20, .h_scaler3[2]   = 0x20, .h_scaler3[3]   = 0x20, .h_scaler3[4]   = 0x20, .h_scaler3[5]   = 0x20, .h_scaler3[6]   = 0x20, .h_scaler3[7]   = 0x20, .h_scaler3[8]   = 0x20, .h_scaler3[9]   = 0x20, .h_scaler3[10]   = 0x20,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x80, .h_scaler9[1]   = 0x80, .h_scaler9[2]   = 0x80, .h_scaler9[3]   = 0x80, .h_scaler9[4]   = 0x80, .h_scaler9[5]   = 0x80, .h_scaler9[6]   = 0x80, .h_scaler9[7]   = 0x80, .h_scaler9[8]   = 0x80, .h_scaler9[9]   = 0x80, .h_scaler9[10]   = 0x80,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0A, .format_set1[1] = 0x0A, .format_set1[2] = 0x0A, .format_set1[3] = 0x0A, .format_set1[4] = 0x0A, .format_set1[5] = 0x0A, .format_set1[6] = 0x0A, .format_set1[7] = 0x0A, .format_set1[8] = 0x0A, .format_set1[9] = 0x0A, .format_set1[10] = 0x0A,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x20, .v_delay[1]     = 0x20, .v_delay[2]     = 0x20, .v_delay[3]     = 0x20, .v_delay[4]     = 0x20, .v_delay[5]     = 0x20, .v_delay[6]     = 0x20, .v_delay[7]     = 0x20, .v_delay[8]     = 0x20, .v_delay[9]     = 0x20, .v_delay[10]     = 0x20,
		},
	},


	[ TVI_HD_B_25P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x27, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7c, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x91, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x84, .contrast[1] 		= 0x80, .contrast[2] 		= 0x80, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x80, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x20, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x70, .h_peaking[9] 		= 0x70, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x82, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x06, .hue[1] 			= 0x06, .hue[2] 			= 0x06, .hue[3] 			= 0x06, .hue[4] 			= 0x06, .hue[5] 			= 0x06, .hue[6] 			= 0x06, .hue[7] 			= 0x06, .hue[8] 			= 0x06, .hue[9] 			= 0x06, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x50, .u_gain[5] 			= 0x50, .u_gain[6] 			= 0x50, .u_gain[7] 			= 0x50, .u_gain[8] 			= 0x50, .u_gain[9] 			= 0x50, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0xf8, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0xf8, .v_gain[3] 			= 0xf8, .v_gain[4] 			= 0xf8, .v_gain[5] 			= 0xf8, .v_gain[6] 			= 0xf8, .v_gain[7] 			= 0xf8, .v_gain[8] 			= 0xf8, .v_gain[9] 			= 0xf8, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0x04, .u_offset[9] 		= 0x04, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0xf4, .v_offset[1] 		= 0xf4, .v_offset[2] 		= 0xf4, .v_offset[3] 		= 0xf4, .v_offset[4] 		= 0xf4, .v_offset[5] 		= 0xf4, .v_offset[6] 		= 0xf4, .v_offset[7] 		= 0xf4, .v_offset[8] 		= 0xf4, .v_offset[9] 		= 0xf4, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x86, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x86, .black_level[5] 	= 0x8c, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x8c, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x50, .acc_ref[7]			= 0x50, .acc_ref[8]			= 0x50, .acc_ref[9]			= 0x50, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0, .sub_saturation[6] 	= 0xf0, .sub_saturation[7] 	= 0xc0, .sub_saturation[8] 	= 0xc0, .sub_saturation[9] 	= 0xc0, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x60, .h_delay_a[1] = 0x62, .h_delay_a[2] = 0x62, .h_delay_a[3] = 0x62, .h_delay_a[4] = 0x62, .h_delay_a[5] = 0x62, .h_delay_a[6] = 0x7f, .h_delay_a[7] = 0x7f, .h_delay_a[8] = 0x7f, .h_delay_a[9] = 0x7f, .h_delay_a[10] = 0x7f,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0b, .format_set1[1] = 0x0b, .format_set1[2] = 0x0b, .format_set1[3] = 0x0b, .format_set1[4] = 0x0b, .format_set1[5] = 0x0b, .format_set1[6] = 0x0b, .format_set1[7] = 0x0b, .format_set1[8] = 0x0b, .format_set1[9] = 0x0b, .format_set1[10] = 0x0b,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03, .format_set2[6] = 0x03, .format_set2[7] = 0x03, .format_set2[8] = 0x03, .format_set2[9] = 0x03, .format_set2[10] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},

	[ TVI_HD_B_30P_EX ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x27, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7c, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x91, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x84, .contrast[1] 		= 0x80, .contrast[2] 		= 0x80, .contrast[3] 		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x80, .contrast[10] 		= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x20, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x70, .h_peaking[9] 		= 0x70, .h_peaking[10] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x82, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 		= 0xb2,
			.hue[0] 			= 0x06, .hue[1] 			= 0x06, .hue[2] 			= 0x06, .hue[3] 			= 0x06, .hue[4] 			= 0x06, .hue[5] 			= 0x06, .hue[6] 			= 0x06, .hue[7] 			= 0x06, .hue[8] 			= 0x06, .hue[9] 			= 0x06, .hue[10] 			= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x50, .u_gain[5] 			= 0x50, .u_gain[6] 			= 0x50, .u_gain[7] 			= 0x50, .u_gain[8] 			= 0x50, .u_gain[9] 			= 0x50, .u_gain[10] 		= 0x00,
			.v_gain[0] 			= 0xf8, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0xf8, .v_gain[3] 			= 0xf8, .v_gain[4] 			= 0xf8, .v_gain[5] 			= 0xf8, .v_gain[6] 			= 0xf8, .v_gain[7] 			= 0xf8, .v_gain[8] 			= 0xf8, .v_gain[9] 			= 0xf8, .v_gain[10] 		= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0x04, .u_offset[9] 		= 0x04, .u_offset[10] 		= 0x00,
			.v_offset[0] 		= 0xf4, .v_offset[1] 		= 0xf4, .v_offset[2] 		= 0xf4, .v_offset[3] 		= 0xf4, .v_offset[4] 		= 0xf4, .v_offset[5] 		= 0xf4, .v_offset[6] 		= 0xf4, .v_offset[7] 		= 0xf4, .v_offset[8] 		= 0xf4, .v_offset[9] 		= 0xf4, .v_offset[10] 		= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x86, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x86, .black_level[5] 	= 0x8c, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x8c, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x50, .acc_ref[7]			= 0x50, .acc_ref[8]			= 0x50, .acc_ref[9]			= 0x50, .acc_ref[10]		= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]		= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0, .sub_saturation[6] 	= 0xf0, .sub_saturation[7] 	= 0xc0, .sub_saturation[8] 	= 0xc0, .sub_saturation[9] 	= 0xc0, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 		= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x5f, .h_delay_a[1] = 0x62, .h_delay_a[2] = 0x62, .h_delay_a[3] = 0x63, .h_delay_a[4] = 0x63, .h_delay_a[5] = 0x63, .h_delay_a[6] = 0x7f, .h_delay_a[7] = 0x7f, .h_delay_a[8] = 0x7f, .h_delay_a[9] = 0x7f, .h_delay_a[10] = 0x7f,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x59,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x0a, .format_set1[1] = 0x0a, .format_set1[2] = 0x0a, .format_set1[3] = 0x0a, .format_set1[4] = 0x0a, .format_set1[5] = 0x0a, .format_set1[6] = 0x0a, .format_set1[7] = 0x0a, .format_set1[8] = 0x0a, .format_set1[9] = 0x0a, .format_set1[10] = 0x0a,
			/*B0 0x85*/	.format_set2[0] = 0x03, .format_set2[1] = 0x03, .format_set2[2] = 0x03, .format_set2[3] = 0x03, .format_set2[4] = 0x03, .format_set2[5] = 0x03, .format_set2[6] = 0x03, .format_set2[7] = 0x03, .format_set2[8] = 0x03, .format_set2[9] = 0x03, .format_set2[10] = 0x03,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},

	[ TVI_HD_50P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x27, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7c, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x91, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 	 	= 0x84, .contrast[1] 	 	= 0x80, .contrast[2] 		= 0x80, .contrast[3]		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x80, .contrast[10] 			= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x20, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x70, .h_peaking[9] 		= 0x70, .h_peaking[10] 			= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 			= 0xb2,
			.hue[0] 			= 0x06, .hue[1] 			= 0x06, .hue[2] 			= 0x06, .hue[3] 			= 0x06, .hue[4] 			= 0x06, .hue[5] 			= 0x06, .hue[6] 			= 0x06, .hue[7] 			= 0x06, .hue[8] 			= 0x06, .hue[9] 			= 0x06, .hue[10] 				= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x50, .u_gain[5] 			= 0x50, .u_gain[6] 			= 0x50, .u_gain[7] 			= 0x50, .u_gain[8] 			= 0x50, .u_gain[9] 			= 0x50, .u_gain[10] 			= 0xe0,
			.v_gain[0] 			= 0xf8, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0xf8, .v_gain[3] 			= 0xf8, .v_gain[4] 			= 0xf8, .v_gain[5] 			= 0xf8, .v_gain[6] 			= 0xf8, .v_gain[7] 			= 0xf8, .v_gain[8] 			= 0xf8, .v_gain[9] 			= 0xf8, .v_gain[10] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0x04, .u_offset[9] 		= 0x04, .u_offset[10] 			= 0xff,
			.v_offset[0] 		= 0xf4, .v_offset[1] 		= 0xf4, .v_offset[2] 		= 0xf4, .v_offset[3] 		= 0xf4, .v_offset[4] 		= 0xf4, .v_offset[5] 		= 0xf4, .v_offset[6] 		= 0xf4, .v_offset[7] 		= 0xf4, .v_offset[8] 		= 0xf4, .v_offset[9] 		= 0xf4, .v_offset[10] 			= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x86, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x86, .black_level[5] 	= 0x8c, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x8c, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x50, .acc_ref[7]			= 0x50, .acc_ref[8]			= 0x50, .acc_ref[9]			= 0x50, .acc_ref[10]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]			= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0, .sub_saturation[6] 	= 0xf0, .sub_saturation[7] 	= 0xc0, .sub_saturation[8] 	= 0xc0, .sub_saturation[9] 	= 0xc0, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 			= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]			= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x64, .h_delay_a[1] = 0x7e, .h_delay_a[2] = 0x7d, .h_delay_a[3] = 0x7c, .h_delay_a[4] = 0x7c, .h_delay_a[5] = 0x7d, .h_delay_a[6] = 0x7d, .h_delay_a[7] = 0x7d, .h_delay_a[8] = 0x7d, .h_delay_a[9] = 0x7d, .h_delay_a[10] = 0x7d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x05, .format_set1[1] = 0x05, .format_set1[2] = 0x05, .format_set1[3] = 0x05, .format_set1[4] = 0x05, .format_set1[5] = 0x05, .format_set1[6] = 0x05, .format_set1[7] = 0x05, .format_set1[8] = 0x05, .format_set1[9] = 0x05, .format_set1[10] = 0x05,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},

	[ TVI_HD_60P ] = /* o */
	{
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x22, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22, .eq_bypass[6] 	= 0x22, .eq_bypass[7] 	= 0x22, .eq_bypass[8] 	= 0x22, .eq_bypass[9] 	= 0x22, .eq_bypass[10] 	 = 0x22,  // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x67, .eq_band_sel[2] = 0x67, .eq_band_sel[3] = 0x67, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x47, .eq_band_sel[6] = 0x27, .eq_band_sel[7] = 0x27, .eq_band_sel[8] = 0x27, .eq_band_sel[9] = 0x27, .eq_band_sel[10] = 0x07, // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7c, .eq_gain_sel[3] = 0x7c, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f, .eq_gain_sel[6] = 0x7f, .eq_gain_sel[7] = 0x7f, .eq_gain_sel[8] = 0x7f, .eq_gain_sel[9] = 0x7f, .eq_gain_sel[10] = 0x7f, // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01, .deq_a_on[6] 	= 0x01, .deq_a_on[7] 	= 0x01, .deq_a_on[8] 	= 0x01, .deq_a_on[9] 	= 0x01, .deq_a_on[10] 	 = 0x01, // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x91, .deq_a_sel[2] 	= 0x92, .deq_a_sel[3] 	= 0x93, .deq_a_sel[4] 	= 0x93, .deq_a_sel[5] 	= 0x92, .deq_a_sel[6] 	= 0x91, .deq_a_sel[7] 	= 0x92, .deq_a_sel[8] 	= 0x93, .deq_a_sel[9] 	= 0x93, .deq_a_sel[10] 	 = 0x92, // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00, .deq_b_sel[6] 	= 0x00, .deq_b_sel[7] 	= 0x00, .deq_b_sel[8] 	= 0x00, .deq_b_sel[9] 	= 0x00, .deq_b_sel[10] 	 = 0x00, // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,	.deqA_01[6] = 0xAC,  .deqA_01[7] = 0xAC, .deqA_01[8] = 0xAC, .deqA_01[9] = 0xAC, .deqA_01[10] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,	.deqA_02[6] = 0x78,	 .deqA_02[7] = 0x78, .deqA_02[8] = 0x78, .deqA_02[9] = 0x78, .deqA_02[10] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,	.deqA_03[6] = 0x17,	 .deqA_03[7] = 0x17, .deqA_03[8] = 0x17, .deqA_03[9] = 0x17, .deqA_03[10] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, .deqA_04[6] = 0xC1,  .deqA_04[7] = 0xC1, .deqA_04[8] = 0xC1, .deqA_04[9] = 0xC1, .deqA_04[10] = 0xC1,	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, .deqA_05[6] = 0x40,  .deqA_05[7] = 0x40, .deqA_05[8] = 0x40, .deqA_05[9] = 0x40, .deqA_05[10] = 0x40,	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, .deqA_06[6] = 0x00,  .deqA_06[7] = 0x00, .deqA_06[8] = 0x00, .deqA_06[9] = 0x00, .deqA_06[10] = 0x00,	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, .deqA_07[6] = 0xC3,  .deqA_07[7] = 0xC3, .deqA_07[8] = 0xC3, .deqA_07[9] = 0xC3, .deqA_07[10] = 0xC3,	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, .deqA_08[6] = 0x0A,  .deqA_08[7] = 0x0A, .deqA_08[8] = 0x0A, .deqA_08[9] = 0x0A, .deqA_08[10] = 0x0A,	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, .deqA_09[6] = 0x00,  .deqA_09[7] = 0x00, .deqA_09[8] = 0x00, .deqA_09[9] = 0x00, .deqA_09[10] = 0x00,	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, .deqA_10[6] = 0x02,  .deqA_10[7] = 0x02, .deqA_10[8] = 0x02, .deqA_10[9] = 0x02, .deqA_10[10] = 0x02,	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, .deqA_11[6] = 0x00,  .deqA_11[7] = 0x00, .deqA_11[8] = 0x00, .deqA_11[9] = 0x00, .deqA_11[10] = 0x00,	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, .deqA_12[6] = 0xB2,  .deqA_12[7] = 0xB2, .deqA_12[8] = 0xB2, .deqA_12[9] = 0xB2, .deqA_12[10] = 0xB2,	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 	 	= 0x84, .contrast[1] 	 	= 0x80, .contrast[2] 		= 0x80, .contrast[3]		= 0x80, .contrast[4] 		= 0x80, .contrast[5] 		= 0x80, .contrast[6] 		= 0x80, .contrast[7] 		= 0x80, .contrast[8] 		= 0x80, .contrast[9] 		= 0x80, .contrast[10] 			= 0x6d,
			.h_peaking[0] 		= 0x00, .h_peaking[1] 		= 0x20, .h_peaking[2] 		= 0x20, .h_peaking[3] 		= 0x20, .h_peaking[4] 		= 0x20, .h_peaking[5] 		= 0x00, .h_peaking[6] 		= 0x70, .h_peaking[7] 		= 0x70, .h_peaking[8] 		= 0x70, .h_peaking[9] 		= 0x70, .h_peaking[10] 			= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92, .c_filter[6] 		= 0x92, .c_filter[7] 		= 0x92, .c_filter[8] 		= 0x92, .c_filter[9] 		= 0x92, .c_filter[10] 			= 0xb2,
			.hue[0] 			= 0x06, .hue[1] 			= 0x06, .hue[2] 			= 0x06, .hue[3] 			= 0x06, .hue[4] 			= 0x06, .hue[5] 			= 0x06, .hue[6] 			= 0x06, .hue[7] 			= 0x06, .hue[8] 			= 0x06, .hue[9] 			= 0x06, .hue[10] 				= 0x00,
			.u_gain[0] 			= 0x50, .u_gain[1] 			= 0x50, .u_gain[2] 			= 0x50, .u_gain[3] 			= 0x50, .u_gain[4] 			= 0x50, .u_gain[5] 			= 0x50, .u_gain[6] 			= 0x50, .u_gain[7] 			= 0x50, .u_gain[8] 			= 0x50, .u_gain[9] 			= 0x50, .u_gain[10] 			= 0xe0,
			.v_gain[0] 			= 0xf8, .v_gain[1] 			= 0xf8, .v_gain[2] 			= 0xf8, .v_gain[3] 			= 0xf8, .v_gain[4] 			= 0xf8, .v_gain[5] 			= 0xf8, .v_gain[6] 			= 0xf8, .v_gain[7] 			= 0xf8, .v_gain[8] 			= 0xf8, .v_gain[9] 			= 0xf8, .v_gain[10] 			= 0x00,
			.u_offset[0] 		= 0x04, .u_offset[1] 		= 0x04, .u_offset[2] 		= 0x04, .u_offset[3] 		= 0x04, .u_offset[4] 		= 0x04, .u_offset[5] 		= 0x04, .u_offset[6] 		= 0x04, .u_offset[7] 		= 0x04, .u_offset[8] 		= 0x04, .u_offset[9] 		= 0x04, .u_offset[10] 			= 0xff,
			.v_offset[0] 		= 0xf4, .v_offset[1] 		= 0xf4, .v_offset[2] 		= 0xf4, .v_offset[3] 		= 0xf4, .v_offset[4] 		= 0xf4, .v_offset[5] 		= 0xf4, .v_offset[6] 		= 0xf4, .v_offset[7] 		= 0xf4, .v_offset[8] 		= 0xf4, .v_offset[9] 		= 0xf4, .v_offset[10] 			= 0x00,

			.black_level[0] 	= 0x86, .black_level[1] 	= 0x86, .black_level[2] 	= 0x86, .black_level[3] 	= 0x86, .black_level[4] 	= 0x86, .black_level[5] 	= 0x8c, .black_level[6] 	= 0x8c, .black_level[7] 	= 0x8c, .black_level[8] 	= 0x8c, .black_level[9] 	= 0x8c, .black_level[10] 	= 0x98,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57, .acc_ref[6]			= 0x50, .acc_ref[7]			= 0x50, .acc_ref[8]			= 0x50, .acc_ref[9]			= 0x50, .acc_ref[10]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80, .cti_delay[6]		= 0x80, .cti_delay[7]		= 0x80, .cti_delay[8]		= 0x80, .cti_delay[9]		= 0x80, .cti_delay[10]			= 0x80,
			.sub_saturation[0] 	= 0xf0, .sub_saturation[1] 	= 0xf0, .sub_saturation[2] 	= 0xf0, .sub_saturation[3] 	= 0xf0, .sub_saturation[4] 	= 0xf0, .sub_saturation[5] 	= 0xf0, .sub_saturation[6] 	= 0xf0, .sub_saturation[7] 	= 0xc0, .sub_saturation[8] 	= 0xc0, .sub_saturation[9] 	= 0xc0, .sub_saturation[10] = 0x50,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a, .burst_dec_a[6] 	= 0x2a, .burst_dec_a[7] 	= 0x2a, .burst_dec_a[8] 	= 0x2a, .burst_dec_a[9] 	= 0x2a, .burst_dec_a[10] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00, .burst_dec_b[6] 	= 0x00, .burst_dec_b[7] 	= 0x00, .burst_dec_b[8] 	= 0x00, .burst_dec_b[9] 	= 0x00, .burst_dec_b[10] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00, .burst_dec_c[6] 	= 0x00, .burst_dec_c[7] 	= 0x00, .burst_dec_c[8] 	= 0x00, .burst_dec_c[9] 	= 0x00, .burst_dec_c[10] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0xb0, .c_option[4] 		= 0xb0, .c_option[5] 		= 0xb0, .c_option[6] 		= 0xb0, .c_option[7] 		= 0xb0, .c_option[8] 		= 0xb0, .c_option[9] 		= 0xb0, .c_option[10] 			= 0xb0,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10, .y_filter_b[6]		= 0x10, .y_filter_b[7]		= 0x10, .y_filter_b[8]		= 0x10, .y_filter_b[9]		= 0x10, .y_filter_b[10]			= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e, .y_filter_b_sel[6]	= 0x1e, .y_filter_b_sel[7]	= 0x1e, .y_filter_b_sel[8]	= 0x1e, .y_filter_b_sel[9]	= 0x1e, .y_filter_b_sel[10]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x64, .h_delay_a[1] = 0x7e, .h_delay_a[2] = 0x7d, .h_delay_a[3] = 0x7c, .h_delay_a[4] = 0x7c, .h_delay_a[5] = 0x7d, .h_delay_a[6] = 0x7d, .h_delay_a[7] = 0x7d, .h_delay_a[8] = 0x7d, .h_delay_a[9] = 0x7d, .h_delay_a[10] = 0x7d,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00, .h_delay_b[6] = 0x00, .h_delay_b[7] = 0x00, .h_delay_b[8] = 0x00, .h_delay_b[9] = 0x00, .h_delay_b[10] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00, .h_delay_c[6] = 0x00, .h_delay_c[7] = 0x00, .h_delay_c[8] = 0x00, .h_delay_c[9] = 0x00, .h_delay_c[10] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00, .y_delay[6] =   0x00, .y_delay[7] =   0x00, .y_delay[8] =   0x00, .y_delay[9] =   0x00, .y_delay[10] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02, .clk_adc[6] = 0x02, .clk_adc[7] = 0x02, .clk_adc[8] = 0x02, .clk_adc[9] = 0x02, .clk_adc[10] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40, .clk_dec[6] = 0x40, .clk_dec[7] = 0x40, .clk_dec[8] = 0x40, .clk_dec[9] = 0x40, .clk_dec[10] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00, .h_scaler1[6]   = 0x00, .h_scaler1[7]   = 0x00, .h_scaler1[8]   = 0x00, .h_scaler1[9]   = 0x00, .h_scaler1[10]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00, .h_scaler2[6]   = 0x00, .h_scaler2[7]   = 0x00, .h_scaler2[8]   = 0x00, .h_scaler2[9]   = 0x00, .h_scaler2[10]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00, .h_scaler3[6]   = 0x00, .h_scaler3[7]   = 0x00, .h_scaler3[8]   = 0x00, .h_scaler3[9]   = 0x00, .h_scaler3[10]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00, .h_scaler4[6]   = 0x00, .h_scaler4[7]   = 0x00, .h_scaler4[8]   = 0x00, .h_scaler4[9]   = 0x00, .h_scaler4[10]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00, .h_scaler5[6]   = 0x00, .h_scaler5[7]   = 0x00, .h_scaler5[8]   = 0x00, .h_scaler5[9]   = 0x00, .h_scaler5[10]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00, .h_scaler6[6]   = 0x00, .h_scaler6[7]   = 0x00, .h_scaler6[8]   = 0x00, .h_scaler6[9]   = 0x00, .h_scaler6[10]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00, .h_scaler7[6]   = 0x00, .h_scaler7[7]   = 0x00, .h_scaler7[8]   = 0x00, .h_scaler7[9]   = 0x00, .h_scaler7[10]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00, .h_scaler8[6]   = 0x00, .h_scaler8[7]   = 0x00, .h_scaler8[8]   = 0x00, .h_scaler8[9]   = 0x00, .h_scaler8[10]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00, .h_scaler9[6]   = 0x00, .h_scaler9[7]   = 0x00, .h_scaler9[8]   = 0x00, .h_scaler9[9]   = 0x00, .h_scaler9[10]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00, .pn_auto[6]		= 0x00, .pn_auto[7]    	= 0x00, .pn_auto[8]    	= 0x00, .pn_auto[9]    	= 0x00, .pn_auto[10]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]   = 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]   = 0x05, .comb_mode[3]   = 0x05, .comb_mode[4]  	= 0x05, .comb_mode[5]   = 0x05, .comb_mode[6]	= 0x05, .comb_mode[7]   = 0x05, .comb_mode[8]   = 0x05, .comb_mode[9]  	= 0x05, .comb_mode[10]   = 0x05,
			/*B9 0xb9*/	.h_pll_op_a[0]  = 0x72, .h_pll_op_a[1]  = 0x72, .h_pll_op_a[2]  = 0x72, .h_pll_op_a[3]  = 0x72, .h_pll_op_a[4]  = 0x72, .h_pll_op_a[5]  = 0x72, .h_pll_op_a[6]  = 0x72, .h_pll_op_a[7]  = 0x72, .h_pll_op_a[8]  = 0x72, .h_pll_op_a[9]  = 0x72, .h_pll_op_a[10]  = 0x72,
			/*B9 0x57*/	.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10, .mem_path[6]	= 0x10, .mem_path[7]	= 0x10, .mem_path[8]	= 0x10, .mem_path[9]	= 0x10, .mem_path[10]	 = 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc, .fsc_lock_speed[6] = 0xdc, .fsc_lock_speed[7] = 0xdc, .fsc_lock_speed[8] = 0xdc, .fsc_lock_speed[9] = 0xdc, .fsc_lock_speed[10] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x04, .format_set1[1] = 0x04, .format_set1[2] = 0x04, .format_set1[3] = 0x04, .format_set1[4] = 0x04, .format_set1[5] = 0x04, .format_set1[6] = 0x04, .format_set1[7] = 0x04, .format_set1[8] = 0x04, .format_set1[9] = 0x04, .format_set1[10] = 0x04,
			/*B0 0x85*/	.format_set2[0] = 0x01, .format_set2[1] = 0x01, .format_set2[2] = 0x01, .format_set2[3] = 0x01, .format_set2[4] = 0x01, .format_set2[5] = 0x01, .format_set2[6] = 0x01, .format_set2[7] = 0x01, .format_set2[8] = 0x01, .format_set2[9] = 0x01, .format_set2[10] = 0x01,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00, .v_delay[6]     = 0x00, .v_delay[7]     = 0x00, .v_delay[8]     = 0x00, .v_delay[9]     = 0x00, .v_delay[10]     = 0x00,
		},
	},

	[ AHD20_SD_H960_NT ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80,
			.h_delay_b[0] = 0x10,
			.h_delay_c[0] = 0x04,
			.y_delay[0]   = 0x27,

		},
		/* clk */
		{
			.clk_adc[0] = 0x06,
			.clk_dec[0] = 0xa6,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			//	/*B9 0x40*/	.pn_auto[0]    	= 0x60,
			/*B9 0x40*/	.pn_auto[0]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x00,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0xc0,
		},
	},

	[ AHD20_SD_H960_PAL ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62,    // Bank5 0x30
			.eq_band_sel[0] = 0x71,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x1a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x54,
			.h_delay_b[0] = 0x10,
			.h_delay_c[0] = 0x07,
			.y_delay[0]   = 0x18,

		},
		/* clk */
		{
			.clk_adc[0] = 0x06,
			.clk_dec[0] = 0xa6,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0xb2,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xcc,

			/*B0 0x81*/	.format_set1[0] = 0x10,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x80,
		},
	},
	[ AHD20_SD_H960_EX_NT ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0xC0,
			.h_delay_b[0] = 0x00,
			.h_delay_c[0] = 0x00,
			.y_delay[0]   = 0x18,

		},
		/* clk */
		{
			.clk_adc[0] = 0x06,
			.clk_dec[0] = 0xa6,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x60,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0xa0,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0xc0,
		},
	},

	[ AHD20_SD_H960_EX_PAL ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80,
			.h_delay_b[0] = 0x10,
			.h_delay_c[0] = 0x07,
			.y_delay[0]   = 0x18,

		},
		/* clk */
		{
			.clk_adc[0] = 0x06,
			.clk_dec[0] = 0xa6,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0xb0,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x80,
		},
	},
	[ AHD20_SD_H960_2EX_Btype_NT ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62,    // Bank5 0x30
			.eq_band_sel[0] = 0x77,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80,
			.h_delay_b[0] = 0x00,
			.h_delay_c[0] = 0x00,
			.y_delay[0]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02,
			.clk_dec[0] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0xE0,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0xe0,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0xc0,
		},
	},

	[ AHD20_SD_H960_2EX_Btype_PAL ] = { /* o */
		/* base */
		{
			.eq_bypass[0] 	= 0x62,    // Bank5 0x30
			.eq_band_sel[0] = 0x77,    // BankA 0x31
			.eq_gain_sel[0] = 0x78,    // BankA 0x32
			.deq_a_on[0] 	= 0x00,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC,	// BankA 0x30
			.deqA_02[0] = 0x78,	// BankA 0x31
			.deqA_03[0] = 0x17,	// BankA 0x32
			.deqA_04[0] = 0xC1, // BankA 0x33
			.deqA_05[0] = 0x40, // BankA 0x34
			.deqA_06[0] = 0x00, // BankA 0x35
			.deqA_07[0] = 0xC3, // BankA 0x36
			.deqA_08[0] = 0x0A, // BankA 0x37
			.deqA_09[0] = 0x00, // BankA 0x38
			.deqA_10[0] = 0x02, // BankA 0x39
			.deqA_11[0] = 0x00, // BankA 0x3a
			.deqA_12[0] = 0xB2, // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x90,
			.h_peaking[0] 		= 0x08,
			.c_filter[0]		= 0x82,

			.hue[0] 			= 0x00,
			.u_gain[0] 			= 0x00,
			.v_gain[0] 			= 0x00,
			.u_offset[0] 		= 0x00,
			.v_offset[0] 		= 0x00,

			.black_level[0] 	= 0x90,
			.acc_ref[0]			= 0x57,
			.cti_delay[0]		= 0x80,
			.sub_saturation[0] 	= 0xc0,

			.burst_dec_a[0] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00,
			.burst_dec_c[0] 	= 0x00,

			.c_option[0] 		= 0x80,

			.y_filter_b[0]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80,
			.h_delay_b[0] = 0x00,
			.h_delay_c[0] = 0x00,
			.y_delay[0]   = 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02,
			.clk_dec[0] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x10,
			/*B9 0x97*/	.h_scaler2[0]  = 0x10,
			/*B9 0x98*/	.h_scaler3[0]  = 0xC0,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00,
			.fsc_lock_speed[0] = 0xcc,

			/*B0 0x81*/	.format_set1[0] = 0xf0,
			/*B0 0x85*/	.format_set2[0] = 0x00,

			/*B0 0x64*/ .v_delay[0]     = 0x80,
		},
	},

	[ TVI_8M_15P ] = { /* o */
		/* base */
		{
			.eq_bypass[0]	= 0x22, .eq_bypass[1]	= 0x22, .eq_bypass[2]	= 0x22, .eq_bypass[3]	= 0x22, .eq_bypass[4]	= 0x22, .eq_bypass[5]	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0]	= 0x00, .deq_a_on[1]	= 0x01, .deq_a_on[2]	= 0x01, .deq_a_on[3]	= 0x01, .deq_a_on[4]	= 0x01, .deq_a_on[5]	= 0x01,    // BankA 0x33
			.deq_a_sel[0]	= 0x00, .deq_a_sel[1]	= 0x92, .deq_a_sel[2]	= 0x93, .deq_a_sel[3]	= 0x95, .deq_a_sel[4]	= 0x94, .deq_a_sel[5]	= 0x95,    // BankA 0x34
			.deq_b_sel[0]	= 0x00, .deq_b_sel[1]	= 0x00, .deq_b_sel[2]	= 0x00, .deq_b_sel[3]	= 0x00, .deq_b_sel[4]	= 0x00, .deq_b_sel[5]	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC, 	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78, 	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17, 	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, 	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, 	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, 	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, 	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, 	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, 	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, 	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, 	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, 	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0]		= 0x88, .contrast[1]		= 0x88, .contrast[2]		= 0x88, .contrast[3]		= 0x88, .contrast[4]		= 0x88, .contrast[5]		= 0x88,
			.h_peaking[0]		= 0x20, .h_peaking[1]		= 0x20, .h_peaking[2]		= 0x30, .h_peaking[3]		= 0x30, .h_peaking[4]		= 0x30, .h_peaking[5]		= 0x30,
			.c_filter[0]		= 0x92, .c_filter[1]		= 0x92, .c_filter[2]		= 0x92, .c_filter[3]		= 0xa2, .c_filter[4]		= 0xb2, .c_filter[5]		= 0xb2,

			.hue[0] 			= 0x05, .hue[1] 			= 0x05, .hue[2] 			= 0x05, .hue[3] 			= 0x05, .hue[4] 			= 0x05, .hue[5] 			= 0x05,
			.u_gain[0]			= 0x50, .u_gain[1]			= 0x50, .u_gain[2]			= 0x14, .u_gain[3]			= 0x14, .u_gain[4]			= 0x14, .u_gain[5]			= 0x14,
			.v_gain[0]			= 0xe0, .v_gain[1]			= 0xf8, .v_gain[2]			= 0x14, .v_gain[3]			= 0x14, .v_gain[4]			= 0x14, .v_gain[5]			= 0x14,
			.u_offset[0]		= 0xfb, .u_offset[1]		= 0xfb, .u_offset[2]		= 0xf6, .u_offset[3]		= 0xf6, .u_offset[4]		= 0xf6, .u_offset[5]		= 0xf6,
			.v_offset[0]		= 0xfc, .v_offset[1]		= 0xfc, .v_offset[2]		= 0xfc, .v_offset[3]		= 0xfc, .v_offset[4]		= 0xfc, .v_offset[5]		= 0xfc,

			.black_level[0] 	= 0x85, .black_level[1] 	= 0x85, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8b, .black_level[4] 	= 0x8b, .black_level[5] 	= 0x8b,
			.acc_ref[0] 		= 0x57, .acc_ref[1] 		= 0x57, .acc_ref[2] 		= 0x57, .acc_ref[3] 		= 0x57, .acc_ref[4] 		= 0x47, .acc_ref[5] 		= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0]	= 0xd0, .sub_saturation[1]	= 0xd0, .sub_saturation[2]	= 0xa0, .sub_saturation[3]	= 0x90, .sub_saturation[4]	= 0x48, .sub_saturation[5]	= 0x20,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0]		= 0xc0, .c_option[1]		= 0xc0, .c_option[2]		= 0xc0, .c_option[3]		= 0xc0, .c_option[4]		= 0xc0, .c_option[5]		= 0xc0,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e, .y_filter_b_sel[1]	= 0x1e, .y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,

		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x6a, .h_delay_a[1] = 0x6a, .h_delay_a[2] = 0x6a, .h_delay_a[3] = 0x6a, .h_delay_a[4] = 0x6a, .h_delay_a[5] = 0x6a,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x02, .h_delay_c[1] = 0x02, .h_delay_c[2] = 0x02, .h_delay_c[3] = 0x02, .h_delay_c[4] = 0x02, .h_delay_c[5] = 0x02,
			.y_delay[0]   = 0x00, .y_delay[1]	= 0x00, .y_delay[2]   = 0x00, .y_delay[3]	= 0x00, .y_delay[4]   = 0x00, .y_delay[5]	= 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]	= 0x00, .h_scaler1[1]	= 0x00, .h_scaler1[2]	= 0x00, .h_scaler1[3]	= 0x00, .h_scaler1[4]	= 0x00, .h_scaler1[5]	= 0x00,
			.h_scaler2[0]	= 0x00, .h_scaler2[1]	= 0x00, .h_scaler2[2]	= 0x00, .h_scaler2[3]	= 0x00, .h_scaler2[4]	= 0x00, .h_scaler2[5]	= 0x00,
			.h_scaler3[0]	= 0x00, .h_scaler3[1]	= 0x00, .h_scaler3[2]	= 0x00, .h_scaler3[3]	= 0x00, .h_scaler3[4]	= 0x00, .h_scaler3[5]	= 0x00,
			.h_scaler4[0]	= 0x00, .h_scaler4[1]	= 0x00, .h_scaler4[2]	= 0x00, .h_scaler4[3]	= 0x00, .h_scaler4[4]	= 0x00, .h_scaler4[5]	= 0x00,
			.h_scaler5[0]	= 0x00, .h_scaler5[1]	= 0x00, .h_scaler5[2]	= 0x00, .h_scaler5[3]	= 0x00, .h_scaler5[4]	= 0x00, .h_scaler5[5]	= 0x00,
			.h_scaler6[0]	= 0x00, .h_scaler6[1]	= 0x00, .h_scaler6[2]	= 0x00, .h_scaler6[3]	= 0x00, .h_scaler6[4]	= 0x00, .h_scaler6[5]	= 0x00,
			.h_scaler7[0]	= 0x00, .h_scaler7[1]	= 0x00, .h_scaler7[2]	= 0x00, .h_scaler7[3]	= 0x00, .h_scaler7[4]	= 0x00, .h_scaler7[5]	= 0x00,
			.h_scaler8[0]	= 0x00, .h_scaler8[1]	= 0x00, .h_scaler8[2]	= 0x00, .h_scaler8[3]	= 0x00, .h_scaler8[4]	= 0x00, .h_scaler8[5]	= 0x00,
			.h_scaler9[0]	= 0x00, .h_scaler9[1]	= 0x00, .h_scaler9[2]	= 0x00, .h_scaler9[3]	= 0x00, .h_scaler9[4]	= 0x00, .h_scaler9[5]	= 0x00,
			.pn_auto[0] 	= 0x00, .pn_auto[1] 	= 0x00, .pn_auto[2] 	= 0x00, .pn_auto[3] 	= 0x00, .pn_auto[4] 	= 0x00, .pn_auto[5] 	= 0x00,
			.comb_mode[0]	= 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]	= 0x05, .comb_mode[3]	= 0x05, .comb_mode[4]	= 0x05, .comb_mode[5]	= 0x05,
			.h_pll_op_a[0]	= 0x72, .h_pll_op_a[1]	= 0x72, .h_pll_op_a[2]	= 0x72, .h_pll_op_a[3]	= 0x72, .h_pll_op_a[4]	= 0x72, .h_pll_op_a[5]	= 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x02, .format_set1[1] = 0x02, .format_set1[2] = 0x02, .format_set1[3] = 0x02, .format_set1[4] = 0x02, .format_set1[5] = 0x02,
			.format_set2[0] = 0x09, .format_set2[1] = 0x09, .format_set2[2] = 0x09, .format_set2[3] = 0x09, .format_set2[4] = 0x09, .format_set2[5] = 0x09,

			.v_delay[0] 	= 0x00, .v_delay[1] 	= 0x00, .v_delay[2] 	= 0x00, .v_delay[3] 	= 0x00, .v_delay[4] 	= 0x00, .v_delay[5] 	= 0x00,
		},
	},
	[ TVI_8M_12_5P ] = { /* o */
		/* base */
		{
			.eq_bypass[0]	= 0x22, .eq_bypass[1]	= 0x22, .eq_bypass[2]	= 0x22, .eq_bypass[3]	= 0x22, .eq_bypass[4]	= 0x22, .eq_bypass[5]	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x57, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x7a, .eq_gain_sel[2] = 0x7f, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0]	= 0x00, .deq_a_on[1]	= 0x01, .deq_a_on[2]	= 0x01, .deq_a_on[3]	= 0x01, .deq_a_on[4]	= 0x01, .deq_a_on[5]	= 0x01,    // BankA 0x33
			.deq_a_sel[0]	= 0x00, .deq_a_sel[1]	= 0x92, .deq_a_sel[2]	= 0x93, .deq_a_sel[3]	= 0x95, .deq_a_sel[4]	= 0x94, .deq_a_sel[5]	= 0x95,    // BankA 0x34
			.deq_b_sel[0]	= 0x00, .deq_b_sel[1]	= 0x00, .deq_b_sel[2]	= 0x00, .deq_b_sel[3]	= 0x00, .deq_b_sel[4]	= 0x00, .deq_b_sel[5]	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC, 	// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78, 	// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17, 	// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1, 	// BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40, 	// BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00, 	// BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3, 	// BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A, 	// BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00, 	// BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02, 	// BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00, 	// BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2, 	// BankA 0x3b
		},
		/* color */
		{
			.contrast[0]		= 0x7e, .contrast[1]		= 0x7c, .contrast[2]		= 0x7a, .contrast[3]		= 0x79, .contrast[4]		= 0x79, .contrast[5]		= 0x79,
			.h_peaking[0]		= 0x20, .h_peaking[1]		= 0x20, .h_peaking[2]		= 0x30, .h_peaking[3]		= 0x30, .h_peaking[4]		= 0x30, .h_peaking[5]		= 0x30,
			.c_filter[0]		= 0x81, .c_filter[1]		= 0x91, .c_filter[2]		= 0x91, .c_filter[3]		= 0xa2, .c_filter[4]		= 0xb2, .c_filter[5]		= 0xb2,

			.hue[0] 			= 0x00, .hue[1] 			= 0x00, .hue[2] 			= 0x00, .hue[3] 			= 0x00, .hue[4] 			= 0x00, .hue[5] 			= 0x00,
			.u_gain[0]			= 0xd8, .u_gain[1]			= 0xf0, .u_gain[2]			= 0x14, .u_gain[3]			= 0x14, .u_gain[4]			= 0x14, .u_gain[5]			= 0x14,
			.v_gain[0]			= 0xe0, .v_gain[1]			= 0xf8, .v_gain[2]			= 0x14, .v_gain[3]			= 0x14, .v_gain[4]			= 0x14, .v_gain[5]			= 0x14,
			.u_offset[0]		= 0xfb, .u_offset[1]		= 0xfb, .u_offset[2]		= 0xf6, .u_offset[3]		= 0xf6, .u_offset[4]		= 0xf6, .u_offset[5]		= 0xf6,
			.v_offset[0]		= 0xfc, .v_offset[1]		= 0xfc, .v_offset[2]		= 0xfc, .v_offset[3]		= 0xfc, .v_offset[4]		= 0xfc, .v_offset[5]		= 0xfc,

			.black_level[0] 	= 0x85, .black_level[1] 	= 0x85, .black_level[2] 	= 0x88, .black_level[3] 	= 0x8b, .black_level[4] 	= 0x8b, .black_level[5] 	= 0x8b,
			.acc_ref[0] 		= 0x57, .acc_ref[1] 		= 0x57, .acc_ref[2] 		= 0x57, .acc_ref[3] 		= 0x57, .acc_ref[4] 		= 0x47, .acc_ref[5] 		= 0x37,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0]	= 0xb0, .sub_saturation[1]	= 0x90, .sub_saturation[2]	= 0x90, .sub_saturation[3]	= 0x80, .sub_saturation[4]	= 0x48, .sub_saturation[5]	= 0x20,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0]		= 0x90, .c_option[1]		= 0x90, .c_option[2]		= 0x90, .c_option[3]		= 0x90, .c_option[4]		= 0x90, .c_option[5]		= 0x90,
			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e, .y_filter_b_sel[1]	= 0x1e, .y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x80, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x10, .h_delay_b[1] = 0x10, .h_delay_b[2] = 0x10, .h_delay_b[3] = 0x10, .h_delay_b[4] = 0x10, .h_delay_b[5] = 0x10,
			.h_delay_c[0] = 0x02, .h_delay_c[1] = 0x02, .h_delay_c[2] = 0x02, .h_delay_c[3] = 0x02, .h_delay_c[4] = 0x02, .h_delay_c[5] = 0x02,
			.y_delay[0]   = 0x00, .y_delay[1]	= 0x00, .y_delay[2]   = 0x00, .y_delay[3]	= 0x00, .y_delay[4]   = 0x00, .y_delay[5]	= 0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x05, .clk_adc[1] = 0x05, .clk_adc[2] = 0x05, .clk_adc[3] = 0x05, .clk_adc[4] = 0x05, .clk_adc[5] = 0x05,
			.clk_dec[0] = 0x44, .clk_dec[1] = 0x44, .clk_dec[2] = 0x44, .clk_dec[3] = 0x44, .clk_dec[4] = 0x44, .clk_dec[5] = 0x44,
		},
		/* timing_b */
		{
			.h_scaler1[0]	= 0x00, .h_scaler1[1]	= 0x00, .h_scaler1[2]	= 0x00, .h_scaler1[3]	= 0x00, .h_scaler1[4]	= 0x00, .h_scaler1[5]	= 0x00,
			.h_scaler2[0]	= 0x00, .h_scaler2[1]	= 0x00, .h_scaler2[2]	= 0x00, .h_scaler2[3]	= 0x00, .h_scaler2[4]	= 0x00, .h_scaler2[5]	= 0x00,
			.h_scaler3[0]	= 0x00, .h_scaler3[1]	= 0x00, .h_scaler3[2]	= 0x00, .h_scaler3[3]	= 0x00, .h_scaler3[4]	= 0x00, .h_scaler3[5]	= 0x00,
			.h_scaler4[0]	= 0x00, .h_scaler4[1]	= 0x00, .h_scaler4[2]	= 0x00, .h_scaler4[3]	= 0x00, .h_scaler4[4]	= 0x00, .h_scaler4[5]	= 0x00,
			.h_scaler5[0]	= 0x00, .h_scaler5[1]	= 0x00, .h_scaler5[2]	= 0x00, .h_scaler5[3]	= 0x00, .h_scaler5[4]	= 0x00, .h_scaler5[5]	= 0x00,
			.h_scaler6[0]	= 0x00, .h_scaler6[1]	= 0x00, .h_scaler6[2]	= 0x00, .h_scaler6[3]	= 0x00, .h_scaler6[4]	= 0x00, .h_scaler6[5]	= 0x00,
			.h_scaler7[0]	= 0x00, .h_scaler7[1]	= 0x00, .h_scaler7[2]	= 0x00, .h_scaler7[3]	= 0x00, .h_scaler7[4]	= 0x00, .h_scaler7[5]	= 0x00,
			.h_scaler8[0]	= 0x00, .h_scaler8[1]	= 0x00, .h_scaler8[2]	= 0x00, .h_scaler8[3]	= 0x00, .h_scaler8[4]	= 0x00, .h_scaler8[5]	= 0x00,
			.h_scaler9[0]	= 0x00, .h_scaler9[1]	= 0x00, .h_scaler9[2]	= 0x00, .h_scaler9[3]	= 0x00, .h_scaler9[4]	= 0x00, .h_scaler9[5]	= 0x00,

			.pn_auto[0] 	= 0x00, .pn_auto[1] 	= 0x00, .pn_auto[2] 	= 0x00, .pn_auto[3] 	= 0x00, .pn_auto[4] 	= 0x00, .pn_auto[5] 	= 0x00,
			.comb_mode[0]	= 0x05, .comb_mode[1]	= 0x05, .comb_mode[2]	= 0x05, .comb_mode[3]	= 0x05, .comb_mode[4]	= 0x05, .comb_mode[5]	= 0x05,
			.h_pll_op_a[0]	= 0x72, .h_pll_op_a[1]	= 0x72, .h_pll_op_a[2]	= 0x72, .h_pll_op_a[3]	= 0x72, .h_pll_op_a[4]	= 0x72, .h_pll_op_a[5]	= 0x72,
			.mem_path[0]	= 0x10, .mem_path[1]	= 0x10, .mem_path[2]	= 0x10, .mem_path[3]	= 0x10, .mem_path[4]	= 0x10, .mem_path[5]	= 0x10,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			.format_set1[0] = 0x01, .format_set1[1] = 0x01, .format_set1[2] = 0x01, .format_set1[3] = 0x01, .format_set1[4] = 0x01, .format_set1[5] = 0x01,
			.format_set2[0] = 0x09, .format_set2[1] = 0x09, .format_set2[2] = 0x09, .format_set2[3] = 0x09, .format_set2[4] = 0x09, .format_set2[5] = 0x09,

			.v_delay[0] 	= 0x00, .v_delay[1] 	= 0x00, .v_delay[2] 	= 0x00, .v_delay[3] 	= 0x00, .v_delay[4] 	= 0x00, .v_delay[5] 	= 0x00,
		},
	},

	[ AHD20_960P_30P ] = {
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x3b, .h_delay_a[1] = 0x3b, .h_delay_a[2] = 0x3b, .h_delay_a[3] = 0x3b, .h_delay_a[4] = 0x3b, .h_delay_a[5] = 0x3b,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x06, .format_set1[1] = 0x06, .format_set1[2] = 0x06, .format_set1[3] = 0x06, .format_set1[4] = 0x06, .format_set1[5] = 0x06,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x29, .v_delay[1]     = 0x29, .v_delay[2]     = 0x29, .v_delay[3]     = 0x29, .v_delay[4]     = 0x29, .v_delay[5]     = 0x29,
		},
	},

	[ AHD20_960P_25P ] = {
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x38, .h_delay_a[1] = 0x38, .h_delay_a[2] = 0x38, .h_delay_a[3] = 0x38, .h_delay_a[4] = 0x38, .h_delay_a[5] = 0x38,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x07, .format_set1[1] = 0x07, .format_set1[2] = 0x07, .format_set1[3] = 0x07, .format_set1[4] = 0x07, .format_set1[5] = 0x07,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x28, .v_delay[1]     = 0x28, .v_delay[2]     = 0x28, .v_delay[3]     = 0x28, .v_delay[4]     = 0x28, .v_delay[5]     = 0x28,
		},
	},

	[ AHD20_960P_60P ] = {
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x84, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x04, .format_set1[1] = 0x04, .format_set1[2] = 0x04, .format_set1[3] = 0x04, .format_set1[4] = 0x04, .format_set1[5] = 0x04,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},

	[ AHD20_960P_50P ] = {
		/* base */
		{
			.eq_bypass[0] 	= 0x62, .eq_bypass[1] 	= 0x62, .eq_bypass[2] 	= 0x22, .eq_bypass[3] 	= 0x22, .eq_bypass[4] 	= 0x22, .eq_bypass[5] 	= 0x22,    // Bank5 0x30
			.eq_band_sel[0] = 0x77, .eq_band_sel[1] = 0x77, .eq_band_sel[2] = 0x77, .eq_band_sel[3] = 0x77, .eq_band_sel[4] = 0x67, .eq_band_sel[5] = 0x57,    // BankA 0x31
			.eq_gain_sel[0] = 0x78, .eq_gain_sel[1] = 0x78, .eq_gain_sel[2] = 0x7a, .eq_gain_sel[3] = 0x7f, .eq_gain_sel[4] = 0x7f, .eq_gain_sel[5] = 0x7f,    // BankA 0x32
			.deq_a_on[0] 	= 0x00, .deq_a_on[1] 	= 0x01, .deq_a_on[2] 	= 0x01, .deq_a_on[3] 	= 0x01, .deq_a_on[4] 	= 0x01, .deq_a_on[5] 	= 0x01,    // BankA 0x33
			.deq_a_sel[0] 	= 0x00, .deq_a_sel[1] 	= 0x92, .deq_a_sel[2] 	= 0x88, .deq_a_sel[3] 	= 0x89, .deq_a_sel[4] 	= 0x8e, .deq_a_sel[5] 	= 0x94,    // BankA 0x34
			.deq_b_sel[0] 	= 0x00, .deq_b_sel[1] 	= 0x00, .deq_b_sel[2] 	= 0x00, .deq_b_sel[3] 	= 0x00, .deq_b_sel[4] 	= 0x00, .deq_b_sel[5] 	= 0x00,    // BankA 0x35
		},
		/* coeff */
		{
			.deqA_01[0] = 0xAC, .deqA_01[1] = 0xAC, .deqA_01[2] = 0xAC, .deqA_01[3] = 0xAC, .deqA_01[4] = 0xAC, .deqA_01[5] = 0xAC,		// BankA 0x30
			.deqA_02[0] = 0x78, .deqA_02[1] = 0x78, .deqA_02[2] = 0x78, .deqA_02[3] = 0x78, .deqA_02[4] = 0x78, .deqA_02[5] = 0x78,		// BankA 0x31
			.deqA_03[0] = 0x17, .deqA_03[1] = 0x17, .deqA_03[2] = 0x17, .deqA_03[3] = 0x17, .deqA_03[4] = 0x17, .deqA_03[5] = 0x17,		// BankA 0x32
			.deqA_04[0] = 0xC1, .deqA_04[1] = 0xC1, .deqA_04[2] = 0xC1, .deqA_04[3] = 0xC1, .deqA_04[4] = 0xC1, .deqA_04[5] = 0xC1,     // BankA 0x33
			.deqA_05[0] = 0x40, .deqA_05[1] = 0x40, .deqA_05[2] = 0x40, .deqA_05[3] = 0x40, .deqA_05[4] = 0x40, .deqA_05[5] = 0x40,     // BankA 0x34
			.deqA_06[0] = 0x00, .deqA_06[1] = 0x00, .deqA_06[2] = 0x00, .deqA_06[3] = 0x00, .deqA_06[4] = 0x00, .deqA_06[5] = 0x00,     // BankA 0x35
			.deqA_07[0] = 0xC3, .deqA_07[1] = 0xC3, .deqA_07[2] = 0xC3, .deqA_07[3] = 0xC3, .deqA_07[4] = 0xC3, .deqA_07[5] = 0xC3,     // BankA 0x36
			.deqA_08[0] = 0x0A, .deqA_08[1] = 0x0A, .deqA_08[2] = 0x0A, .deqA_08[3] = 0x0A, .deqA_08[4] = 0x0A, .deqA_08[5] = 0x0A,     // BankA 0x37
			.deqA_09[0] = 0x00, .deqA_09[1] = 0x00, .deqA_09[2] = 0x00, .deqA_09[3] = 0x00, .deqA_09[4] = 0x00, .deqA_09[5] = 0x00,     // BankA 0x38
			.deqA_10[0] = 0x02, .deqA_10[1] = 0x02, .deqA_10[2] = 0x02, .deqA_10[3] = 0x02, .deqA_10[4] = 0x02, .deqA_10[5] = 0x02,     // BankA 0x39
			.deqA_11[0] = 0x00, .deqA_11[1] = 0x00, .deqA_11[2] = 0x00, .deqA_11[3] = 0x00, .deqA_11[4] = 0x00, .deqA_11[5] = 0x00,     // BankA 0x3a
			.deqA_12[0] = 0xB2, .deqA_12[1] = 0xB2, .deqA_12[2] = 0xB2, .deqA_12[3] = 0xB2, .deqA_12[4] = 0xB2, .deqA_12[5] = 0xB2,     // BankA 0x3b
		},
		/* color */
		{
			.contrast[0] 		= 0x88, .contrast[1] 		= 0x88, .contrast[2] 		= 0x88, .contrast[3] 		= 0x88, .contrast[4] 		= 0x88, .contrast[5] 		= 0x7e,
			.h_peaking[0] 		= 0x3f, .h_peaking[1] 		= 0x3f, .h_peaking[2] 		= 0x3f, .h_peaking[3] 		= 0x3f, .h_peaking[4] 		= 0x3f, .h_peaking[5] 		= 0x3f,
			.c_filter[0]		= 0x82, .c_filter[1] 		= 0x82, .c_filter[2]	 	= 0x82, .c_filter[3] 		= 0x82, .c_filter[4] 		= 0x92, .c_filter[5] 		= 0x92,

			.hue[0] 			= 0xfe, .hue[1] 			= 0xfe, .hue[2] 			= 0xfe, .hue[3] 			= 0xfe, .hue[4] 			= 0xfe, .hue[5] 			= 0xfe,
			.u_gain[0] 			= 0x30, .u_gain[1] 			= 0x30, .u_gain[2] 			= 0x30, .u_gain[3] 			= 0x30, .u_gain[4] 			= 0x30, .u_gain[5] 			= 0x30,
			.v_gain[0] 			= 0x00, .v_gain[1] 			= 0x00, .v_gain[2] 			= 0xf0, .v_gain[3] 			= 0xf0, .v_gain[4] 			= 0xf0, .v_gain[5] 			= 0xf0,
			.u_offset[0] 		= 0x00, .u_offset[1] 		= 0x00, .u_offset[2] 		= 0x00, .u_offset[3] 		= 0x00, .u_offset[4] 		= 0x00, .u_offset[5] 		= 0x00,
			.v_offset[0] 		= 0x00, .v_offset[1] 		= 0x00, .v_offset[2] 		= 0x00, .v_offset[3] 		= 0x00, .v_offset[4] 		= 0x00, .v_offset[5] 		= 0x00,

			.black_level[0] 	= 0x84, .black_level[1] 	= 0x84, .black_level[2] 	= 0x84, .black_level[3] 	= 0x84, .black_level[4] 	= 0x84, .black_level[5] 	= 0x86,
			.acc_ref[0]			= 0x57, .acc_ref[1]			= 0x57, .acc_ref[2]			= 0x57, .acc_ref[3]			= 0x57, .acc_ref[4]			= 0x57, .acc_ref[5]			= 0x57,
			.cti_delay[0]		= 0x80, .cti_delay[1]		= 0x80, .cti_delay[2]		= 0x80, .cti_delay[3]		= 0x80, .cti_delay[4]		= 0x80, .cti_delay[5]		= 0x80,
			.sub_saturation[0] 	= 0xc0, .sub_saturation[1] 	= 0xc0, .sub_saturation[2] 	= 0xb8, .sub_saturation[3] 	= 0xb8, .sub_saturation[4] 	= 0xb0, .sub_saturation[5] 	= 0xb0,

			.burst_dec_a[0] 	= 0x2a, .burst_dec_a[1] 	= 0x2a, .burst_dec_a[2] 	= 0x2a, .burst_dec_a[3] 	= 0x2a, .burst_dec_a[4] 	= 0x2a, .burst_dec_a[5] 	= 0x2a,
			.burst_dec_b[0] 	= 0x00, .burst_dec_b[1] 	= 0x00, .burst_dec_b[2] 	= 0x00, .burst_dec_b[3] 	= 0x00, .burst_dec_b[4] 	= 0x00, .burst_dec_b[5] 	= 0x00,
			.burst_dec_c[0] 	= 0x00, .burst_dec_c[1] 	= 0x00, .burst_dec_c[2] 	= 0x00, .burst_dec_c[3] 	= 0x00, .burst_dec_c[4] 	= 0x00, .burst_dec_c[5] 	= 0x00,

			.c_option[0] 		= 0x80, .c_option[1] 		= 0x80, .c_option[2] 		= 0x80, .c_option[3] 		= 0x80, .c_option[4] 		= 0x90, .c_option[5] 		= 0x90,

			.y_filter_b[0]		= 0x10, .y_filter_b[1]		= 0x10, .y_filter_b[2]		= 0x10, .y_filter_b[3]		= 0x10, .y_filter_b[4]		= 0x10, .y_filter_b[5]		= 0x10,
			.y_filter_b_sel[0]	= 0x1e,	.y_filter_b_sel[1]	= 0x1e,	.y_filter_b_sel[2]	= 0x1e, .y_filter_b_sel[3]	= 0x1e, .y_filter_b_sel[4]	= 0x1e, .y_filter_b_sel[5]	= 0x1e,
		},
		/* timing_a */
		{
			.h_delay_a[0] = 0x80, .h_delay_a[1] = 0x84, .h_delay_a[2] = 0x80, .h_delay_a[3] = 0x80, .h_delay_a[4] = 0x80, .h_delay_a[5] = 0x80,
			.h_delay_b[0] = 0x00, .h_delay_b[1] = 0x00, .h_delay_b[2] = 0x00, .h_delay_b[3] = 0x00, .h_delay_b[4] = 0x00, .h_delay_b[5] = 0x00,
			.h_delay_c[0] = 0x00, .h_delay_c[1] = 0x00, .h_delay_c[2] = 0x00, .h_delay_c[3] = 0x00, .h_delay_c[4] = 0x00, .h_delay_c[5] = 0x00,
			.y_delay[0]   = 0x00, .y_delay[1]   = 0x00, .y_delay[2]   = 0x00, .y_delay[3]   = 0x00, .y_delay[4]   = 0x00, .y_delay[5] =   0x00,

		},
		/* clk */
		{
			.clk_adc[0] = 0x02, .clk_adc[1] = 0x02, .clk_adc[2] = 0x02, .clk_adc[3] = 0x02, .clk_adc[4] = 0x02, .clk_adc[5] = 0x02,
			.clk_dec[0] = 0x40, .clk_dec[1] = 0x40, .clk_dec[2] = 0x40, .clk_dec[3] = 0x40, .clk_dec[4] = 0x40, .clk_dec[5] = 0x40,
		},
		/* timing_b */
		{
			/*B9 0x96*/	.h_scaler1[0]  = 0x00, .h_scaler1[1]   = 0x00, .h_scaler1[2]   = 0x00, .h_scaler1[3]   = 0x00, .h_scaler1[4]   = 0x00, .h_scaler1[5]   = 0x00,
			/*B9 0x97*/	.h_scaler2[0]  = 0x00, .h_scaler2[1]   = 0x00, .h_scaler2[2]   = 0x00, .h_scaler2[3]   = 0x00, .h_scaler2[4]   = 0x00, .h_scaler2[5]   = 0x00,
			/*B9 0x98*/	.h_scaler3[0]  = 0x00, .h_scaler3[1]   = 0x00, .h_scaler3[2]   = 0x00, .h_scaler3[3]   = 0x00, .h_scaler3[4]   = 0x00, .h_scaler3[5]   = 0x00,
			/*B9 0x99*/	.h_scaler4[0]  = 0x00, .h_scaler4[1]   = 0x00, .h_scaler4[2]   = 0x00, .h_scaler4[3]   = 0x00, .h_scaler4[4]   = 0x00, .h_scaler4[5]   = 0x00,
			/*B9 0x9a*/	.h_scaler5[0]  = 0x00, .h_scaler5[1]   = 0x00, .h_scaler5[2]   = 0x00, .h_scaler5[3]   = 0x00, .h_scaler5[4]   = 0x00, .h_scaler5[5]   = 0x00,
			/*B9 0x9b*/	.h_scaler6[0]  = 0x00, .h_scaler6[1]   = 0x00, .h_scaler6[2]   = 0x00, .h_scaler6[3]   = 0x00, .h_scaler6[4]   = 0x00, .h_scaler6[5]   = 0x00,
			/*B9 0x9c*/	.h_scaler7[0]  = 0x00, .h_scaler7[1]   = 0x00, .h_scaler7[2]   = 0x00, .h_scaler7[3]   = 0x00, .h_scaler7[4]   = 0x00, .h_scaler7[5]   = 0x00,
			/*B9 0x9d*/	.h_scaler8[0]  = 0x00, .h_scaler8[1]   = 0x00, .h_scaler8[2]   = 0x00, .h_scaler8[3]   = 0x00, .h_scaler8[4]   = 0x00, .h_scaler8[5]   = 0x00,
			/*B9 0x9e*/	.h_scaler9[0]  = 0x00, .h_scaler9[1]   = 0x00, .h_scaler9[2]   = 0x00, .h_scaler9[3]   = 0x00, .h_scaler9[4]   = 0x00, .h_scaler9[5]   = 0x00,


			/*B9 0x40*/	.pn_auto[0]    	= 0x00, .pn_auto[1]		= 0x00, .pn_auto[2]    	= 0x00, .pn_auto[3]    	= 0x00, .pn_auto[4]    	= 0x00, .pn_auto[5]    	= 0x00,

			/*B5 0x90*/	.comb_mode[0]      = 0x01, .comb_mode[1]	  = 0x01, .comb_mode[2]      = 0x01, .comb_mode[3]      = 0x01, .comb_mode[4]  	   = 0x01, .comb_mode[5]      = 0x01,
			/*B9 0xb9*/	.h_pll_op_a[0]     = 0x72, .h_pll_op_a[1]     = 0x72, .h_pll_op_a[2]     = 0x72, .h_pll_op_a[3]     = 0x72, .h_pll_op_a[4]     = 0x72, .h_pll_op_a[5]     = 0x72,
			/*B9 0x57*/	.mem_path[0]	   = 0x00, .mem_path[1]	      = 0x00, .mem_path[2]	     = 0x00, .mem_path[3]	    = 0x00, .mem_path[4]	   = 0x00, .mem_path[5]	      = 0x00,
			.fsc_lock_speed[0] = 0xdc, .fsc_lock_speed[1] = 0xdc, .fsc_lock_speed[2] = 0xdc, .fsc_lock_speed[3] = 0xdc, .fsc_lock_speed[4] = 0xdc, .fsc_lock_speed[5] = 0xdc,

			/*B0 0x81*/	.format_set1[0] = 0x05, .format_set1[1] = 0x05, .format_set1[2] = 0x05, .format_set1[3] = 0x05, .format_set1[4] = 0x05, .format_set1[5] = 0x05,
			/*B0 0x85*/	.format_set2[0] = 0x0b, .format_set2[1] = 0x0b, .format_set2[2] = 0x0b, .format_set2[3] = 0x0b, .format_set2[4] = 0x0b, .format_set2[5] = 0x0b,

			/*B0 0x64*/ .v_delay[0]     = 0x00, .v_delay[1]     = 0x00, .v_delay[2]     = 0x00, .v_delay[3]     = 0x00, .v_delay[4]     = 0x00, .v_delay[5]     = 0x00,
		},
	},
};

#endif // End of NVP6168 EQ TABLE
