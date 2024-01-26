/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * File Name     : video_engines.h
 * Description   : AST2600 video  engines
 *
 * Copyright (C) 2019-2021 ASPEED Technology Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VIDEO_ENGINE_H__
#define __VIDEO_ENGINE_H__

#include "video_ioctl.h"
#include "hardware_engines.h"

#define VIDEO_STREAM_BUFFER_SIZE	(0x400000) //4M
#define VIDEO_CAPTURE_BUFFER_SIZE	(0xA00000) //10M
#define VIDEO_JPEG_TABLE_SIZE		(0x100000) //1M

#define SCU_VIDEO_ENGINE_BIT						BIT(6)
#define SCU_VIDEO_CAPTURE_STOP_CLOCK_BIT			BIT(3)
#define SCU_VIDEO_ENGINE_STOP_CLOCK_BIT				BIT(1)
/***********************************************************************/
/* Register for VIDEO */
#define AST_VIDEO_PROTECT			0x000		/*	protection key register	*/
#define AST_VIDEO_SEQ_CTRL			0x004		/*	Video Sequence Control register	*/
#define AST_VIDEO_PASS_CTRL		0x008		/*	Video Pass 1 Control register	*/

//VR008[5]=1
#define AST_VIDEO_DIRECT_BASE		0x00C		/*	Video Direct Frame buffer mode control Register VR008[5]=1 */
#define AST_VIDEO_DIRECT_CTRL		0x010		/*	Video Direct Frame buffer mode control Register VR008[5]=1 */

//VR008[5]=0
#define AST_VIDEO_TIMING_H			0x00C		/*	Video Timing Generation Setting Register */
#define AST_VIDEO_TIMING_V			0x010		/*	Video Timing Generation Setting Register */
#define AST_VIDEO_SCAL_FACTOR		0x014		/*	Video Scaling Factor Register */

#define AST_VIDEO_SCALING0		0x018		/*	Video Scaling Filter Parameter Register #0 */
#define AST_VIDEO_SCALING1		0x01C		/*	Video Scaling Filter Parameter Register #1 */
#define AST_VIDEO_SCALING2		0x020		/*	Video Scaling Filter Parameter Register #2 */
#define AST_VIDEO_SCALING3		0x024		/*	Video Scaling Filter Parameter Register #3 */

#define AST_VIDEO_BCD_CTRL			0x02C		/*	Video BCD Control Register */
#define AST_VIDEO_CAPTURE_WIN		0x030		/*	 Video Capturing Window Setting Register */
#define AST_VIDEO_COMPRESS_WIN	0x034		/*	 Video Compression Window Setting Register */

#define AST_VIDEO_COMPRESS_PRO	0x038		/* Video Compression Stream Buffer Processing Offset Register */
#define AST_VIDEO_COMPRESS_READ	0x03C		/* Video Compression Stream Buffer Read Offset Register */

#define AST_VIDEO_JPEG_HEADER_BUFF	0x040		/*	Video Based Address of JPEG Header Buffer Register */
#define AST_VIDEO_SOURCE_BUFF0		0x044		/*	Video Based Address of Video Source Buffer #1 Register */
#define AST_VIDEO_SOURCE_SCAN_LINE	0x048		/*	Video Scan Line Offset of Video Source Buffer Register */
#define AST_VIDEO_SOURCE_BUFF1		0x04C		/*	Video Based Address of Video Source Buffer #2 Register */
#define AST_VIDEO_BCD_BUFF				0x050		/*	Video Base Address of BCD Flag Buffer Register */
#define AST_VIDEO_STREAM_BUFF			0x054		/*	Video Base Address of Compressed Video Stream Buffer Register */
#define AST_VIDEO_STREAM_SIZE			0x058		/*	Video Stream Buffer Size Register */

#define AST_VIDEO_COMPRESS_CTRL				0x060		/* Video Compression Control Register */

#define AST_VIDEO_COMPRESS_DATA_COUNT		0x070		/* Video Total Size of Compressed Video Stream Read Back Register */
#define AST_VIDEO_COMPRESS_BLOCK_COUNT		0x074		/* Video Total Number of Compressed Video Block Read Back Register */
#define AST_VIDEO_COMPRESS_FRAME_END		0x078		/* Video Frame-end offset of compressed video stream buffer read back Register */

#define AST_VIDEO_CTRL			0x300		/* Video Control Register */
#define AST_VIDEO_INT_EN		0x304		/* Video interrupt Enable */
#define AST_VIDEO_INT_STS		0x308		/* Video interrupt status */
#define AST_VIDEO_MODE_DETECT	0x30C		/* Video Mode Detection Parameter Register */

#define AST_VIDEO_CRC1			0x320		/* Primary CRC Parameter Register */
#define AST_VIDEO_CRC2			0x324		/* Second CRC Parameter Register */
#define AST_VIDEO_DATA_TRUNCA	0x328		/* Video Data Truncation Register */

#define AST_VIDEO_E_SCRATCH_34C	0x34C		/* Video Scratch Remap Read Back */
#define AST_VIDEO_E_SCRATCH_350	0x350		/* Video Scratch Remap Read Back */
#define AST_VIDEO_E_SCRATCH_354	0x354		/* Video Scratch Remap Read Back */

//multi jpeg
#define AST_VIDEO_ENCRYPT_SRAM		0x400		/* Video RC4/AES128 Encryption Key Register #0 ~ #63 */
#define AST_VIDEO_MULTI_JPEG_SRAM	(AST_VIDEO_ENCRYPT_SRAM)		/* Multi JPEG registers */

#define REG_32_BIT_SZ_IN_BYTES (sizeof(u32))

#define SET_FRAME_W_H(w, h) ((((u32)(h)) & 0x1fff) | ((((u32)(w)) & 0x1fff) << 13))
#define SET_FRAME_START_ADDR(addr) ((addr) & 0x7fffff80)

/////////////////////////////////////////////////////////////////////////////

/*	AST_VIDEO_PROTECT: 0x000  - protection key register */
#define VIDEO_PROTECT_UNLOCK				0x1A038AA8

/*	AST_VIDEO_SEQ_CTRL		0x004		Video Sequence Control register	*/
#define VIDEO_HALT_ENG_STS				BIT(21)
#define VIDEO_COMPRESS_BUSY				BIT(18)
#define VIDEO_CAPTURE_BUSY				BIT(16)
#define VIDEO_HALT_ENG_TRIGGER				BIT(12)
#define VIDEO_COMPRESS_FORMAT_MASK				BIT(10)
#define VIDEO_GET_COMPRESS_FORMAT(x)		(((x) >> 10) & 0x3)   // 0 YUV444
#define VIDEO_COMPRESS_FORMAT(x)			((x) << 10)	// 0 YUV444
#define YUV420		1

#define G5_VIDEO_COMPRESS_JPEG_MODE			BIT(13)
#define VIDEO_YUV2RGB_DITHER_EN			BIT(8)

#define VIDEO_COMPRESS_JPEG_MODE			BIT(8)

//if bit 0 : 1
#define VIDEO_INPUT_MODE_CHG_WDT			BIT(7)
#define VIDEO_INSERT_FULL_COMPRESS			BIT(6)
#define VIDEO_AUTO_COMPRESS			BIT(5)
#define VIDEO_COMPRESS_TRIGGER			BIT(4)
#define VIDEO_CAPTURE_MULTI_FRAME			BIT(3)
#define VIDEO_COMPRESS_FORCE_IDLE			BIT(2)
#define VIDEO_CAPTURE_TRIGGER			BIT(1)
#define VIDEO_DETECT_TRIGGER			BIT(0)

#define VIDEO_HALT_ENG_RB			BIT(21)

#define VIDEO_ABCD_CHG_EN			BIT(1)
#define VIDEO_BCD_CHG_EN			(1)

/*	AST_VIDEO_PASS_CTRL			0x008		Video Pass1 Control register	*/
#define G6_VIDEO_MULTI_JPEG_FLAG_MODE		BIT(31)
#define G6_VIDEO_MULTI_JPEG_MODE			BIT(30)
#define G6_VIDEO_JPEG__COUNT(x)			((x) << 24)
#define G6_VIDEO_FRAME_CT_MASK			(0x3f << 24)

/*	AST_VIDEO_DIRECT_CTRL	0x010		Video Direct Frame buffer mode control Register VR008[5]=1 */
#define VIDEO_FETCH_TIMING(x)			((x) << 16)
#define VIDEO_FETCH_LINE_OFFSET(x)		((x) & 0xffff)

//x * source frame rate / 60
#define VIDEO_FRAME_RATE_CTRL(x)			((x) << 16)
#define VIDEO_HSYNC_POLARITY_CTRL			BIT(15)
#define VIDEO_INTERLANCE_MODE			BIT(14)
#define VIDEO_DUAL_EDGE_MODE			BIT(13)	//0 : Single edage
#define VIDEO_18BIT_SINGLE_EDGE			BIT(12)	//0: 24bits
#define VIDEO_DVO_INPUT_DELAY_MASK			(7 << 9)
#define VIDEO_DVO_INPUT_DELAY(x)			((x) << 9) //0 : no delay , 1: 1ns, 2: 2ns, 3:3ns, 4: inversed clock but no delay
// if bit 5 : 0
#define VIDEO_HW_CURSOR_DIS			BIT(8)
// if bit 5 : 1
#define VIDEO_AUTO_FETCH			BIT(8)	//
#define VIDEO_CAPTURE_FORMATE_MASK		(3 << 6)

#define VIDEO_SET_CAPTURE_FORMAT(x)		((x) << 6)
#define JPEG_MODE		1
#define RGB_MODE		2
#define GRAY_MODE		3
#define VIDEO_DIRECT_FETCH				BIT(5)
// if bit 5 : 0
#define VIDEO_INTERNAL_DE				BIT(4)
#define VIDEO_EXT_ADC_ATTRIBUTE				BIT(3)

/*	 AST_VIDEO_CAPTURE_WIN	0x030		Video Capturing Window Setting Register */
#define VIDEO_CAPTURE_V(x)				((x) & 0x7ff)
#define VIDEO_CAPTURE_H(x)				(((x) & 0x7ff) << 16)

/*	 AST_VIDEO_COMPRESS_WIN	0x034		Video Compression Window Setting Register */
#define VIDEO_COMPRESS_V(x)			((x) & 0x7ff)
#define VIDEO_GET_COMPRESS_V(x)			((x) & 0x7ff)
#define VIDEO_COMPRESS_H(x)			(((x) & 0x7ff) << 16)
#define VIDEO_GET_COMPRESS_H(x)			(((x) >> 16) & 0x7ff)

/*	AST_VIDEO_STREAM_SIZE	0x058		Video Stream Buffer Size Register */
#define VIDEO_STREAM_PKT_N(x)			((x) << 3)
#define STREAM_4_PKTS		0
#define STREAM_8_PKTS		1
#define STREAM_16_PKTS		2
#define STREAM_32_PKTS		3
#define STREAM_64_PKTS		4
#define STREAM_128_PKTS		5

#define VIDEO_STREAM_PKT_SIZE(x)		(x)
#define STREAM_1KB		0
#define STREAM_2KB		1
#define STREAM_4KB		2
#define STREAM_8KB		3
#define STREAM_16KB		4
#define STREAM_32KB		5
#define STREAM_64KB		6
#define STREAM_128KB	7

/* AST_VIDEO_COMPRESS_CTRL	0x060		Video Compression Control Register */
#define VIDEO_DCT_CQT_SELECTION			(0xf << 6)  // bit 6-9, bit 10 for which quantization is referred
#define VIDEO_DCT_HQ_CQT_SELECTION		(0xf << 27) // bit 27-30, bit 31 for which quantization is referred

#define VIDEO_HQ_DCT_LUM(x)			((x) << 27)
#define VIDEO_GET_HQ_DCT_LUM(x)			(((x) >> 27) & 0x1f)
#define VIDEO_HQ_DCT_CHROM(x)			((x) << 22)
#define VIDEO_GET_HQ_DCT_CHROM(x)			(((x) >> 22) & 0x1f)
#define VIDEO_HQ_DCT_MASK			(0x3ff << 22)
#define VIDEO_DCT_HUFFMAN_ENCODE(x)			((x) << 20)
#define VIDEO_DCT_RESET				BIT(17)
#define VIDEO_HQ_ENABLE				BIT(16)
#define VIDEO_GET_HQ_ENABLE(x)			(((x) >> 16) & 0x1)
#define VIDEO_DCT_LUM(x)			((x) << 11)
#define VIDEO_GET_DCT_LUM(x)			(((x) >> 11) & 0x1f)
#define VIDEO_DCT_CHROM(x)			((x) << 6)
#define VIDEO_GET_DCT_CHROM(x)			(((x) >> 6) & 0x1f)
#define VIDEO_DCT_MASK				(0x3ff << 6)
#define VIDEO_ENCRYP_ENABLE			BIT(5)
#define VIDEO_COMPRESS_QUANTIZ_MODE			BIT(2)
#define VIDEO_4COLOR_VQ_ENCODE			BIT(1)
#define VIDEO_DCT_ONLY_ENCODE				(1)
#define VIDEO_DCT_VQ_MASK					(0x3)

#define VIDEO_CTRL_RC4_TEST_MODE		BIT(9)
#define VIDEO_CTRL_RC4_RST		BIT(8)

#define VIDEO_CTRL_ADDRESS_MAP_MULTI_JPEG	(0x3 << 30)

#define VIDEO_CTRL_DWN_SCALING_MASK		(0x3 << 4)
#define VIDEO_CTRL_DWN_SCALING_ENABLE_LINE_BUFFER		BIT(4)

/* AST_VIDEO_INT_EN			0x304		Video interrupt Enable */
/* AST_VIDEO_INT_STS		0x308		Video interrupt status */
#define VM_COMPRESS_COMPLETE			BIT(17)
#define VM_CAPTURE_COMPLETE			BIT(16)

#define VIDEO_FRAME_COMPLETE			BIT(5)
#define VIDEO_MODE_DETECT_RDY			BIT(4)
#define VIDEO_COMPRESS_COMPLETE			BIT(3)
#define VIDEO_COMPRESS_PKT_COMPLETE			BIT(2)
#define VIDEO_CAPTURE_COMPLETE			BIT(1)
#define VIDEO_MODE_DETECT_WDT			BIT(0)

/***********************************************************************/
struct VideoMem {
	dma_addr_t	phy;
	void *pVirt;
	u32 size;
};

struct VideoEngineMem {
	struct VideoMem captureBuf0;
	struct VideoMem captureBuf1;
	struct VideoMem jpegTable;
};

struct ast_capture_mode {
	u8	engine_idx;					//set 0: engine 0, engine 1
	u8	differential;					//set 0: full, 1:diff frame
	u8	mode_change;				//get 0: no, 1:change
};

struct ast_compression_mode {
	u8	engine_idx;					//set 0: engine 0, engine 1
	u8	mode_change;				//get 0: no, 1:change
	u32	total_size;					//get
	u32	block_count;					//get
};

/***********************************************************************/
struct INTERNAL_MODE {
	u16 HorizontalActive;
	u16 VerticalActive;
	u16 RefreshRateIndex;
	u32 PixelClock;
};

// ioctl functions
void ioctl_get_video_engine_config(struct VideoConfig  *pVideoConfig, struct AstRVAS *pAstRVAS);
void ioctl_set_video_engine_config(struct VideoConfig  *pVideoConfig, struct AstRVAS *pAstRVAS);
void ioctl_get_video_engine_data(struct MultiJpegConfig *pArrayMJConfig, struct AstRVAS *pAstRVAS,  u32 dwPhyStreamAddress);

//local functions
irqreturn_t ast_video_isr(int this_irq, void *dev_id);
int video_engine_reserveMem(struct AstRVAS *pAstRVAS);
void enable_video_interrupt(struct AstRVAS *pAstRVAS);
void disable_video_interrupt(struct AstRVAS *pAstRVAS);
void video_set_Window(struct AstRVAS *pAstRVAS);
int free_video_engine_memory(struct AstRVAS *pAstRVAS);
void video_ctrl_init(struct AstRVAS *pAstRVAS);
void video_engine_rc4Reset(struct AstRVAS *pAstRVAS);
void set_direct_mode(struct AstRVAS *pAstRVAS);

#endif // __VIDEO_ENGINE_H__
