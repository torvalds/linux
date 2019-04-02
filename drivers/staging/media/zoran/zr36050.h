/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Zoran ZR36050 basic configuration functions - header file
 *
 * Copyright (C) 2001 Wolfgang Scherr <scherr@net4you.at>
 */
#ifndef ZR36050_H
#define ZR36050_H

#include "videocodec.h"

/* data stored for each zoran jpeg codec chip */
struct zr36050 {
	char name[32];
	int num;
	/* io datastructure */
	struct videocodec *codec;
	// last coder status
	__u8 status1;
	// actual coder setup
	int mode;

	__u16 width;
	__u16 height;

	__u16 bitrate_ctrl;

	__u32 total_code_vol;
	__u32 real_code_vol;
	__u16 max_block_vol;

	__u8 h_samp_ratio[8];
	__u8 v_samp_ratio[8];
	__u16 scalefact;
	__u16 dri;

	/* com/app marker */
	struct jpeg_com_marker com;
	struct jpeg_app_marker app;
};

/* zr36050 register addresses */
#define ZR050_GO                  0x000
#define ZR050_HARDWARE            0x002
#define ZR050_MODE                0x003
#define ZR050_OPTIONS             0x004
#define ZR050_MBCV                0x005
#define ZR050_MARKERS_EN          0x006
#define ZR050_INT_REQ_0           0x007
#define ZR050_INT_REQ_1           0x008
#define ZR050_TCV_NET_HI          0x009
#define ZR050_TCV_NET_MH          0x00a
#define ZR050_TCV_NET_ML          0x00b
#define ZR050_TCV_NET_LO          0x00c
#define ZR050_TCV_DATA_HI         0x00d
#define ZR050_TCV_DATA_MH         0x00e
#define ZR050_TCV_DATA_ML         0x00f
#define ZR050_TCV_DATA_LO         0x010
#define ZR050_SF_HI               0x011
#define ZR050_SF_LO               0x012
#define ZR050_AF_HI               0x013
#define ZR050_AF_M                0x014
#define ZR050_AF_LO               0x015
#define ZR050_ACV_HI              0x016
#define ZR050_ACV_MH              0x017
#define ZR050_ACV_ML              0x018
#define ZR050_ACV_LO              0x019
#define ZR050_ACT_HI              0x01a
#define ZR050_ACT_MH              0x01b
#define ZR050_ACT_ML              0x01c
#define ZR050_ACT_LO              0x01d
#define ZR050_ACV_TRUN_HI         0x01e
#define ZR050_ACV_TRUN_MH         0x01f
#define ZR050_ACV_TRUN_ML         0x020
#define ZR050_ACV_TRUN_LO         0x021
#define ZR050_STATUS_0            0x02e
#define ZR050_STATUS_1            0x02f

#define ZR050_SOF_IDX             0x040
#define ZR050_SOS1_IDX            0x07a
#define ZR050_SOS2_IDX            0x08a
#define ZR050_SOS3_IDX            0x09a
#define ZR050_SOS4_IDX            0x0aa
#define ZR050_DRI_IDX             0x0c0
#define ZR050_DNL_IDX             0x0c6
#define ZR050_DQT_IDX             0x0cc
#define ZR050_DHT_IDX             0x1d4
#define ZR050_APP_IDX             0x380
#define ZR050_COM_IDX             0x3c0

/* zr36050 hardware register bits */

#define ZR050_HW_BSWD                0x80
#define ZR050_HW_MSTR                0x40
#define ZR050_HW_DMA                 0x20
#define ZR050_HW_CFIS_1_CLK          0x00
#define ZR050_HW_CFIS_2_CLK          0x04
#define ZR050_HW_CFIS_3_CLK          0x08
#define ZR050_HW_CFIS_4_CLK          0x0C
#define ZR050_HW_CFIS_5_CLK          0x10
#define ZR050_HW_CFIS_6_CLK          0x14
#define ZR050_HW_CFIS_7_CLK          0x18
#define ZR050_HW_CFIS_8_CLK          0x1C
#define ZR050_HW_BELE                0x01

/* zr36050 mode register bits */

#define ZR050_MO_COMP                0x80
#define ZR050_MO_ATP                 0x40
#define ZR050_MO_PASS2               0x20
#define ZR050_MO_TLM                 0x10
#define ZR050_MO_DCONLY              0x08
#define ZR050_MO_BRC                 0x04

#define ZR050_MO_ATP                 0x40
#define ZR050_MO_PASS2               0x20
#define ZR050_MO_TLM                 0x10
#define ZR050_MO_DCONLY              0x08

/* zr36050 option register bits */

#define ZR050_OP_NSCN_1              0x00
#define ZR050_OP_NSCN_2              0x20
#define ZR050_OP_NSCN_3              0x40
#define ZR050_OP_NSCN_4              0x60
#define ZR050_OP_NSCN_5              0x80
#define ZR050_OP_NSCN_6              0xA0
#define ZR050_OP_NSCN_7              0xC0
#define ZR050_OP_NSCN_8              0xE0
#define ZR050_OP_OVF                 0x10


/* zr36050 markers-enable register bits */

#define ZR050_ME_APP                 0x80
#define ZR050_ME_COM                 0x40
#define ZR050_ME_DRI                 0x20
#define ZR050_ME_DQT                 0x10
#define ZR050_ME_DHT                 0x08
#define ZR050_ME_DNL                 0x04
#define ZR050_ME_DQTI                0x02
#define ZR050_ME_DHTI                0x01

/* zr36050 status0/1 register bit masks */

#define ZR050_ST_RST_MASK            0x20
#define ZR050_ST_SOF_MASK            0x02
#define ZR050_ST_SOS_MASK            0x02
#define ZR050_ST_DATRDY_MASK         0x80
#define ZR050_ST_MRKDET_MASK         0x40
#define ZR050_ST_RFM_MASK            0x10
#define ZR050_ST_RFD_MASK            0x08
#define ZR050_ST_END_MASK            0x04
#define ZR050_ST_TCVOVF_MASK         0x02
#define ZR050_ST_DATOVF_MASK         0x01

/* pixel component idx */

#define ZR050_Y_COMPONENT         0
#define ZR050_U_COMPONENT         1
#define ZR050_V_COMPONENT         2

#endif				/*fndef ZR36050_H */
