/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Zoran ZR36060 basic configuration functions - header file
 *
 * Copyright (C) 2002 Laurent Pinchart <laurent.pinchart@skynet.be>
 */
#ifndef ZR36060_H
#define ZR36060_H

#include "videocodec.h"

/* data stored for each zoran jpeg codec chip */
struct zr36060 {
	char name[32];
	int num;
	/* io datastructure */
	struct videocodec *codec;
	// last coder status
	__u8 status;
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

	/* app/com marker data */
	struct jpeg_app_marker app;
	struct jpeg_com_marker com;
};

/* ZR36060 register addresses */
#define ZR060_LOAD			0x000
#define ZR060_CFSR			0x001
#define ZR060_CIR			0x002
#define ZR060_CMR			0x003
#define ZR060_MBZ			0x004
#define ZR060_MBCVR			0x005
#define ZR060_MER			0x006
#define ZR060_IMR			0x007
#define ZR060_ISR			0x008
#define ZR060_TCV_NET_HI		0x009
#define ZR060_TCV_NET_MH		0x00a
#define ZR060_TCV_NET_ML		0x00b
#define ZR060_TCV_NET_LO		0x00c
#define ZR060_TCV_DATA_HI		0x00d
#define ZR060_TCV_DATA_MH		0x00e
#define ZR060_TCV_DATA_ML		0x00f
#define ZR060_TCV_DATA_LO		0x010
#define ZR060_SF_HI			0x011
#define ZR060_SF_LO			0x012
#define ZR060_AF_HI			0x013
#define ZR060_AF_M			0x014
#define ZR060_AF_LO			0x015
#define ZR060_ACV_HI			0x016
#define ZR060_ACV_MH			0x017
#define ZR060_ACV_ML			0x018
#define ZR060_ACV_LO			0x019
#define ZR060_ACT_HI			0x01a
#define ZR060_ACT_MH			0x01b
#define ZR060_ACT_ML			0x01c
#define ZR060_ACT_LO			0x01d
#define ZR060_ACV_TRUN_HI		0x01e
#define ZR060_ACV_TRUN_MH		0x01f
#define ZR060_ACV_TRUN_ML		0x020
#define ZR060_ACV_TRUN_LO		0x021
#define ZR060_IDR_DEV			0x022
#define ZR060_IDR_REV			0x023
#define ZR060_TCR_HI			0x024
#define ZR060_TCR_LO			0x025
#define ZR060_VCR			0x030
#define ZR060_VPR			0x031
#define ZR060_SR			0x032
#define ZR060_BCR_Y			0x033
#define ZR060_BCR_U			0x034
#define ZR060_BCR_V			0x035
#define ZR060_SGR_VTOTAL_HI		0x036
#define ZR060_SGR_VTOTAL_LO		0x037
#define ZR060_SGR_HTOTAL_HI		0x038
#define ZR060_SGR_HTOTAL_LO		0x039
#define ZR060_SGR_VSYNC			0x03a
#define ZR060_SGR_HSYNC			0x03b
#define ZR060_SGR_BVSTART		0x03c
#define ZR060_SGR_BHSTART		0x03d
#define ZR060_SGR_BVEND_HI		0x03e
#define ZR060_SGR_BVEND_LO		0x03f
#define ZR060_SGR_BHEND_HI		0x040
#define ZR060_SGR_BHEND_LO		0x041
#define ZR060_AAR_VSTART_HI		0x042
#define ZR060_AAR_VSTART_LO		0x043
#define ZR060_AAR_VEND_HI		0x044
#define ZR060_AAR_VEND_LO		0x045
#define ZR060_AAR_HSTART_HI		0x046
#define ZR060_AAR_HSTART_LO		0x047
#define ZR060_AAR_HEND_HI		0x048
#define ZR060_AAR_HEND_LO		0x049
#define ZR060_SWR_VSTART_HI		0x04a
#define ZR060_SWR_VSTART_LO		0x04b
#define ZR060_SWR_VEND_HI		0x04c
#define ZR060_SWR_VEND_LO		0x04d
#define ZR060_SWR_HSTART_HI		0x04e
#define ZR060_SWR_HSTART_LO		0x04f
#define ZR060_SWR_HEND_HI		0x050
#define ZR060_SWR_HEND_LO		0x051

#define ZR060_SOF_IDX			0x060
#define ZR060_SOS_IDX			0x07a
#define ZR060_DRI_IDX			0x0c0
#define ZR060_DQT_IDX			0x0cc
#define ZR060_DHT_IDX			0x1d4
#define ZR060_APP_IDX			0x380
#define ZR060_COM_IDX			0x3c0

/* ZR36060 LOAD register bits */

#define ZR060_LOAD_Load			(1 << 7)
#define ZR060_LOAD_SyncRst		(1 << 0)

/* ZR36060 Code FIFO Status register bits */

#define ZR060_CFSR_Busy			(1 << 7)
#define ZR060_CFSR_CBusy		(1 << 2)
#define ZR060_CFSR_CFIFO		(3 << 0)

/* ZR36060 Code Interface register */

#define ZR060_CIR_Code16		(1 << 7)
#define ZR060_CIR_Endian		(1 << 6)
#define ZR060_CIR_CFIS			(1 << 2)
#define ZR060_CIR_CodeMstr		(1 << 0)

/* ZR36060 Codec Mode register */

#define ZR060_CMR_Comp			(1 << 7)
#define ZR060_CMR_ATP			(1 << 6)
#define ZR060_CMR_Pass2			(1 << 5)
#define ZR060_CMR_TLM			(1 << 4)
#define ZR060_CMR_BRB			(1 << 2)
#define ZR060_CMR_FSF			(1 << 1)

/* ZR36060 Markers Enable register */

#define ZR060_MER_App			(1 << 7)
#define ZR060_MER_Com			(1 << 6)
#define ZR060_MER_DRI			(1 << 5)
#define ZR060_MER_DQT			(1 << 4)
#define ZR060_MER_DHT			(1 << 3)

/* ZR36060 Interrupt Mask register */

#define ZR060_IMR_EOAV			(1 << 3)
#define ZR060_IMR_EOI			(1 << 2)
#define ZR060_IMR_End			(1 << 1)
#define ZR060_IMR_DataErr		(1 << 0)

/* ZR36060 Interrupt Status register */

#define ZR060_ISR_ProCnt		(3 << 6)
#define ZR060_ISR_EOAV			(1 << 3)
#define ZR060_ISR_EOI			(1 << 2)
#define ZR060_ISR_End			(1 << 1)
#define ZR060_ISR_DataErr		(1 << 0)

/* ZR36060 Video Control register */

#define ZR060_VCR_Video8		(1 << 7)
#define ZR060_VCR_Range			(1 << 6)
#define ZR060_VCR_FIDet			(1 << 3)
#define ZR060_VCR_FIVedge		(1 << 2)
#define ZR060_VCR_FIExt			(1 << 1)
#define ZR060_VCR_SyncMstr		(1 << 0)

/* ZR36060 Video Polarity register */

#define ZR060_VPR_VCLKPol		(1 << 7)
#define ZR060_VPR_PValPol		(1 << 6)
#define ZR060_VPR_PoePol		(1 << 5)
#define ZR060_VPR_SImgPol		(1 << 4)
#define ZR060_VPR_BLPol			(1 << 3)
#define ZR060_VPR_FIPol			(1 << 2)
#define ZR060_VPR_HSPol			(1 << 1)
#define ZR060_VPR_VSPol			(1 << 0)

/* ZR36060 Scaling register */

#define ZR060_SR_VScale			(1 << 2)
#define ZR060_SR_HScale2		(1 << 0)
#define ZR060_SR_HScale4		(2 << 0)

#endif				/*fndef ZR36060_H */
