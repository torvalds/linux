// SPDX-License-Identifier: GPL-2.0+
/*
 * File Name     : video_engines.c
 * Description   : AST2600 video  engines
 *
 * Copyright (C) 2019-2021 ASPEED Technology Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/reset.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/hwmon-sysfs.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <asm/uaccess.h>

#include "video_ioctl.h"
#include "video_engine.h"
#include "video_debug.h"
#include "hardware_engines.h"

static struct VideoEngineMem vem;

//
//functions
//
static inline void video_write(struct AstRVAS *pAstRVAS, u32 val, u32 reg);
static inline u32 video_read(struct AstRVAS *pAstRVAS, u32 reg);

static u32 get_vga_mem_base(struct AstRVAS *pAstRVAS);
static int reserve_video_engine_memory(struct AstRVAS *pAstRVAS);
static void init_jpeg_table(void);
static void video_set_scaling(struct AstRVAS *pAstRVAS);
static int video_capture_trigger(struct AstRVAS *pAstRVAS);
static void dump_buffer(u32 dwPhyStreamAddress, u32 size);

//
// function definitions
//
void ioctl_get_video_engine_config(struct VideoConfig *pVideoConfig, struct AstRVAS *pAstRVAS)
{
	u32 VR004_SeqCtrl = video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL);
	u32 VR060_ComCtrl = video_read(pAstRVAS, AST_VIDEO_COMPRESS_CTRL);

	// status
	pVideoConfig->rs = SuccessStatus;

	pVideoConfig->engine = 0;	// engine = 1 is Video Management
	pVideoConfig->capture_format = 0;
	pVideoConfig->compression_mode = 0;

	pVideoConfig->compression_format = (VR004_SeqCtrl >> 13) & 0x1;
	pVideoConfig->YUV420_mode = (VR004_SeqCtrl >> 10) & 0x3;
	pVideoConfig->AutoMode = (VR004_SeqCtrl >> 5) & 0x1;

	pVideoConfig->rc4_enable = (VR060_ComCtrl >> 5) & 0x1;
	pVideoConfig->Visual_Lossless = (VR060_ComCtrl >> 16) & 0x1;
	pVideoConfig->Y_JPEGTableSelector = VIDEO_GET_DCT_LUM(VR060_ComCtrl);
	pVideoConfig->AdvanceTableSelector = (VR060_ComCtrl >> 27) & 0xf;
}

void ioctl_set_video_engine_config(struct VideoConfig  *pVideoConfig, struct AstRVAS *pAstRVAS)
{
	int i, base = 0;
	u32 ctrl = 0;	//for VR004, VR204
	u32 compress_ctrl = 0x00080000;
	u32 *tlb_table = vem.jpegTable.pVirt;

	// status
	pVideoConfig->rs = SuccessStatus;

	VIDEO_ENG_DBG("\n");

	ctrl = video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL);

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) &
				~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE | G6_VIDEO_MULTI_JPEG_FLAG_MODE), AST_VIDEO_PASS_CTRL);

	ctrl &= ~VIDEO_AUTO_COMPRESS;
	ctrl |= G5_VIDEO_COMPRESS_JPEG_MODE;
	ctrl &= ~VIDEO_COMPRESS_FORMAT_MASK; //~(3<<10) bit 4 is set to 0

	if (pVideoConfig->YUV420_mode)
		ctrl |= VIDEO_COMPRESS_FORMAT(YUV420);

	if (pVideoConfig->rc4_enable)
		compress_ctrl |= VIDEO_ENCRYP_ENABLE;

	switch (pVideoConfig->compression_mode) {
	case 0:	//DCT only
			compress_ctrl |= VIDEO_DCT_ONLY_ENCODE;
			break;
	case 1:	//DCT VQ mix 2-color
			compress_ctrl &= ~(VIDEO_4COLOR_VQ_ENCODE | VIDEO_DCT_ONLY_ENCODE);
			break;
	case 2:	//DCT VQ mix 4-color
			compress_ctrl |= VIDEO_4COLOR_VQ_ENCODE;
			break;
	default:
			dev_err(pAstRVAS->pdev, "unknown compression mode:%d\n", pVideoConfig->compression_mode);
			break;
	}

	if (pVideoConfig->Visual_Lossless) {
		compress_ctrl |= VIDEO_HQ_ENABLE;
		compress_ctrl |= VIDEO_HQ_DCT_LUM(pVideoConfig->AdvanceTableSelector);
		compress_ctrl |= VIDEO_HQ_DCT_CHROM((pVideoConfig->AdvanceTableSelector + 16));
	} else {
		compress_ctrl &= ~VIDEO_HQ_ENABLE;
	}

	video_write(pAstRVAS, ctrl, AST_VIDEO_SEQ_CTRL);
	// we are using chrominance quantization table instead of luminance quantization table
	video_write(pAstRVAS, compress_ctrl | VIDEO_DCT_LUM(pVideoConfig->Y_JPEGTableSelector) | VIDEO_DCT_CHROM(pVideoConfig->Y_JPEGTableSelector + 16), AST_VIDEO_COMPRESS_CTRL);
	VIDEO_ENG_DBG("VR04: %#X\n", video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL));
	VIDEO_ENG_DBG("VR60: %#X\n", video_read(pAstRVAS, AST_VIDEO_COMPRESS_CTRL));

	// chose a table for JPEG or multi-JPEG
	if (pVideoConfig->compression_format >= 1) {
		VIDEO_ENG_DBG("Choose a JPEG Table\n");
		for (i = 0; i < 12; i++) {
			base = (1024 * i);
			//base = (256 * i);
			if (pVideoConfig->YUV420_mode)	//yuv420
				tlb_table[base + 46] = 0x00220103; //for YUV420 mode
			else
				tlb_table[base + 46] = 0x00110103; //for YUV444 mode)
		}
	}

	video_set_scaling(pAstRVAS);
}

//
void ioctl_get_video_engine_data(struct MultiJpegConfig *pArrayMJConfig, struct AstRVAS *pAstRVAS, u32 dwPhyStreamAddress)
{
	u32 yuv_shift;
	u32 yuv_msk;
	u32 scan_lines;
	int timeout = 0;
	u32 x0;
	u32 y0;
	int i = 0;
	u32 dw_w_h;
	u32 start_addr;
	u32 multi_jpeg_data = 0;
	u32 VR044;
	u32 nextFrameOffset = 0;

	pArrayMJConfig->rs = SuccessStatus;

	VIDEO_ENG_DBG("\n");
	VIDEO_ENG_DBG("before Stream buffer:\n");
	//dump_buffer(dwPhyStreamAddress,100);

	video_write(pAstRVAS, dwPhyStreamAddress, AST_VIDEO_STREAM_BUFF);

	if (host_suspended(pAstRVAS)) {
		pArrayMJConfig->rs = HostSuspended;
		VIDEO_ENG_DBG("HostSuspended Timeout\n");
		return;
	}

	if (video_capture_trigger(pAstRVAS) == 0) {
		pArrayMJConfig->rs = CaptureTimedOut;
		VIDEO_ENG_DBG("Capture Timeout\n");
		return;
	}
	//dump_buffer(dwPhyStreamAddress,100);

	init_completion(&pAstRVAS->video_compression_complete);
	VIDEO_ENG_DBG("capture complete buffer:\n");

	//dump_buffer(vem.captureBuf0.phy,100);
	VR044 = video_read(pAstRVAS, AST_VIDEO_SOURCE_BUFF0);

	scan_lines = video_read(pAstRVAS, AST_VIDEO_SOURCE_SCAN_LINE);
	VIDEO_ENG_DBG("scan_lines: %#x\n", scan_lines);

	if (video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) & VIDEO_COMPRESS_FORMAT(YUV420)) {
		// YUV 420
		VIDEO_ENG_DBG("Debug: YUV420\n");
		yuv_shift = 4;
		yuv_msk = 0xf;
	} else {
		// YUV 444
		VIDEO_ENG_DBG("Debug: YUV444\n");
		yuv_shift = 3;
		yuv_msk = 0x7;
	}

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) | G6_VIDEO_MULTI_JPEG_FLAG_MODE |
			(G6_VIDEO_JPEG__COUNT(pArrayMJConfig->multi_jpeg_frames - 1) | G6_VIDEO_MULTI_JPEG_MODE), AST_VIDEO_PASS_CTRL);

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_CTRL) | VIDEO_CTRL_ADDRESS_MAP_MULTI_JPEG, AST_VIDEO_CTRL);

	for (i = 0; i < pArrayMJConfig->multi_jpeg_frames; i++) {
		VIDEO_ENG_DBG("Debug: Before: [%d]: x: %#x y: %#x w: %#x h: %#x\n", i,
			      pArrayMJConfig->frame[i].wXPixels,
			      pArrayMJConfig->frame[i].wYPixels,
			      pArrayMJConfig->frame[i].wWidthPixels,
			      pArrayMJConfig->frame[i].wHeightPixels);
		x0 = pArrayMJConfig->frame[i].wXPixels;
		y0 = pArrayMJConfig->frame[i].wYPixels;
		dw_w_h = SET_FRAME_W_H(pArrayMJConfig->frame[i].wWidthPixels, pArrayMJConfig->frame[i].wHeightPixels);

		start_addr = VR044 + (scan_lines * y0) + ((256 * x0) / (1 << yuv_shift));

		VIDEO_ENG_DBG("VR%x dw_w_h: %#x, VR%x : addr : %#x, x0 %d, y0 %d\n",
			      AST_VIDEO_MULTI_JPEG_SRAM + (8 * i), dw_w_h,
			      AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4, start_addr, x0, y0);
		video_write(pAstRVAS, dw_w_h, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i));
		video_write(pAstRVAS, start_addr, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4);
	}

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER), AST_VIDEO_SEQ_CTRL);

	//set mode for multi-jpeg mode VR004[5:3]
	video_write(pAstRVAS, (video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) & ~VIDEO_AUTO_COMPRESS)
				| VIDEO_CAPTURE_MULTI_FRAME | G5_VIDEO_COMPRESS_JPEG_MODE, AST_VIDEO_SEQ_CTRL);

	//If CPU is too fast, pleas read back and trigger
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) | VIDEO_COMPRESS_TRIGGER, AST_VIDEO_SEQ_CTRL);
	VIDEO_ENG_DBG("wait_for_completion_interruptible_timeout...\n");

	timeout = wait_for_completion_interruptible_timeout(&pAstRVAS->video_compression_complete, HZ / 2);

	if (timeout == 0) {
		dev_err(pAstRVAS->pdev, "multi compression timeout sts %x\n", video_read(pAstRVAS, AST_VIDEO_INT_STS));
		pArrayMJConfig->multi_jpeg_frames = 0;
		pArrayMJConfig->rs = CompressionTimedOut;
	} else {
		VIDEO_ENG_DBG("400 %x , 404 %x\n", video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM), video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM + 4));
		VIDEO_ENG_DBG("408 %x , 40c %x\n", video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM + 8), video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM + 0xC));
		VIDEO_ENG_DBG("done reading 408\n");

		for (i = 0; i < pArrayMJConfig->multi_jpeg_frames; i++) {
			pArrayMJConfig->frame[i].dwOffsetInBytes = nextFrameOffset;

			multi_jpeg_data = video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i) + 4);
			if (multi_jpeg_data & BIT(7)) {
				pArrayMJConfig->frame[i].dwSizeInBytes = video_read(pAstRVAS, AST_VIDEO_MULTI_JPEG_SRAM + (8 * i)) & 0xffffff;
				nextFrameOffset = (multi_jpeg_data & ~BIT(7)) >> 1;
			} else {
				pArrayMJConfig->frame[i].dwSizeInBytes = 0;
				nextFrameOffset = 0;
			}
			VIDEO_ENG_DBG("[%d] size %d, dwOffsetInBytes %x\n", i, pArrayMJConfig->frame[i].dwSizeInBytes, pArrayMJConfig->frame[i].dwOffsetInBytes);
		} //for
	}

	video_write(pAstRVAS, (video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) & ~(G5_VIDEO_COMPRESS_JPEG_MODE | VIDEO_CAPTURE_MULTI_FRAME))
			| VIDEO_AUTO_COMPRESS, AST_VIDEO_SEQ_CTRL);
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) &
			~(G6_VIDEO_FRAME_CT_MASK | G6_VIDEO_MULTI_JPEG_MODE), AST_VIDEO_PASS_CTRL);

	//VIDEO_ENG_DBG("after Stream buffer:\n");
	//dump_buffer(dwPhyStreamAddress,100);
}

irqreturn_t ast_video_isr(int this_irq, void *dev_id)
{
	u32 status;
	//u32 swap0, swap1;
	struct AstRVAS *pAstRVAS = dev_id;

	status = video_read(pAstRVAS, AST_VIDEO_INT_STS);

	VIDEO_ENG_DBG("%x\n", status);

	if (status & VIDEO_COMPRESS_COMPLETE) {
		video_write(pAstRVAS, VIDEO_COMPRESS_COMPLETE, AST_VIDEO_INT_STS);
		VIDEO_ENG_DBG("compress complete swap\n");
		// no need to swap for better performance
		// swap0 = video_read(pAstRVAS, AST_VIDEO_SOURCE_BUFF0);
		// swap1 = video_read(pAstRVAS, AST_VIDEO_SOURCE_BUFF1);
		// video_write(pAstRVAS, swap1, AST_VIDEO_SOURCE_BUFF0);
		// video_write(pAstRVAS, swap0, AST_VIDEO_SOURCE_BUFF1);
		complete(&pAstRVAS->video_compression_complete);
	}
	if (status & VIDEO_CAPTURE_COMPLETE) {
		video_write(pAstRVAS, VIDEO_CAPTURE_COMPLETE, AST_VIDEO_INT_STS);
		VIDEO_ENG_DBG("capture complete\n");
		complete(&pAstRVAS->video_capture_complete);
	}

	return IRQ_HANDLED;
}

void enable_video_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 intCtrReg = video_read(pAstRVAS, AST_VIDEO_INT_EN);

	intCtrReg = (VIDEO_COMPRESS_COMPLETE | VIDEO_CAPTURE_COMPLETE);
	video_write(pAstRVAS, intCtrReg, AST_VIDEO_INT_EN);
}

void disable_video_interrupt(struct AstRVAS *pAstRVAS)
{
	video_write(pAstRVAS, 0, AST_VIDEO_INT_EN);
}

void video_engine_rc4Reset(struct AstRVAS *pAstRVAS)
{
	//rc4 init reset ..
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_CTRL) | VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_CTRL) & ~VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
}

// setup functions
int video_engine_reserveMem(struct AstRVAS *pAstRVAS)
{
	int result = 0;

	// reserve mem
	result = reserve_video_engine_memory(pAstRVAS);
	if (result < 0) {
		dev_err(pAstRVAS->pdev, "Error Reserving Video Engine Memory\n");
		return result;
	}
	return 0;
}

int free_video_engine_memory(struct AstRVAS *pAstRVAS)
{
	int size = vem.captureBuf0.size + vem.captureBuf1.size + vem.jpegTable.size;

	if (size && vem.captureBuf0.pVirt) {
		dma_free_coherent(pAstRVAS->pdev, size,
				  vem.captureBuf0.pVirt,
				  vem.captureBuf0.phy);
	} else {
		return -1;
	}
	VIDEO_ENG_DBG("After dma_free_coherent\n");

	return 0;
}

// this function needs to be called when graphic mode change
void video_set_Window(struct AstRVAS *pAstRVAS)
{
	u32 scan_line;

	VIDEO_ENG_DBG("\n");

	//compression x,y
	video_write(pAstRVAS, VIDEO_COMPRESS_H(pAstRVAS->current_vg.wStride) | VIDEO_COMPRESS_V(pAstRVAS->current_vg.wScreenHeight), AST_VIDEO_COMPRESS_WIN);
	VIDEO_ENG_DBG("reg offset[%#x]: %#x\n", AST_VIDEO_COMPRESS_WIN, video_read(pAstRVAS, AST_VIDEO_COMPRESS_WIN));

	if (pAstRVAS->current_vg.wStride == 1680)
		video_write(pAstRVAS, VIDEO_CAPTURE_H(1728) | VIDEO_CAPTURE_V(pAstRVAS->current_vg.wScreenHeight), AST_VIDEO_CAPTURE_WIN);
	else
		video_write(pAstRVAS, VIDEO_CAPTURE_H(pAstRVAS->current_vg.wStride) | VIDEO_CAPTURE_V(pAstRVAS->current_vg.wScreenHeight), AST_VIDEO_CAPTURE_WIN);

	VIDEO_ENG_DBG("reg offset[%#x]: %#x\n", AST_VIDEO_CAPTURE_WIN, video_read(pAstRVAS, AST_VIDEO_CAPTURE_WIN));

	// set scan_line VR048
	if ((pAstRVAS->current_vg.wStride % 8) == 0) {
		video_write(pAstRVAS, pAstRVAS->current_vg.wStride * 4, AST_VIDEO_SOURCE_SCAN_LINE);
	} else {
		scan_line = pAstRVAS->current_vg.wStride;
		scan_line = scan_line + 16 - (scan_line % 16);
		scan_line = scan_line * 4;
		video_write(pAstRVAS, scan_line, AST_VIDEO_SOURCE_SCAN_LINE);
	}
	VIDEO_ENG_DBG("reg offset[%#x]: %#x\n", AST_VIDEO_SOURCE_SCAN_LINE, video_read(pAstRVAS, AST_VIDEO_SOURCE_SCAN_LINE));
}

void set_direct_mode(struct AstRVAS *pAstRVAS)
{
	int Direct_Mode = 0;
	u32 ColorDepthIndex;
	u32 VGA_Scratch_Register_350, VGA_Scratch_Register_354, VGA_Scratch_Register_34C, Color_Depth;

	VIDEO_ENG_DBG("\n");
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) & ~(VIDEO_AUTO_FETCH | VIDEO_DIRECT_FETCH), AST_VIDEO_PASS_CTRL);

	VGA_Scratch_Register_350 = video_read(pAstRVAS, AST_VIDEO_E_SCRATCH_350);
	VGA_Scratch_Register_34C = video_read(pAstRVAS, AST_VIDEO_E_SCRATCH_34C);
	VGA_Scratch_Register_354 = video_read(pAstRVAS, AST_VIDEO_E_SCRATCH_354);

	if (((VGA_Scratch_Register_350 & 0xff00) >> 8) == 0xA8) {
		Color_Depth = ((VGA_Scratch_Register_350 & 0xff0000) >> 16);

		if (Color_Depth < 15)
			Direct_Mode = 0;
		else
			Direct_Mode = 1;

	} else { //Original mode information
		ColorDepthIndex = (VGA_Scratch_Register_34C >> 4) & 0x0F;

		if (ColorDepthIndex == 0xe || ColorDepthIndex == 0xf) {
			Direct_Mode = 0;
		} else {
			if (ColorDepthIndex > 2)
				Direct_Mode = 1;
			else
				Direct_Mode = 0;
		}
	}

	if (Direct_Mode) {
		VIDEO_ENG_DBG("Direct Mode\n");
		video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) | VIDEO_AUTO_FETCH | VIDEO_DIRECT_FETCH, AST_VIDEO_PASS_CTRL);
		video_write(pAstRVAS, get_vga_mem_base(pAstRVAS), AST_VIDEO_DIRECT_BASE);
		video_write(pAstRVAS, VIDEO_FETCH_TIMING(0) | VIDEO_FETCH_LINE_OFFSET(pAstRVAS->current_vg.wStride * 4), AST_VIDEO_DIRECT_CTRL);
	} else {
		VIDEO_ENG_DBG("Sync None Direct Mode\n");
		video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) & ~(VIDEO_AUTO_FETCH | VIDEO_DIRECT_FETCH), AST_VIDEO_PASS_CTRL);
	}
}

// return timeout 0 - timeout; non 0 is successful
static int video_capture_trigger(struct AstRVAS *pAstRVAS)
{
	int timeout = 0;

	VIDEO_ENG_DBG("\n");

	init_completion(&pAstRVAS->video_capture_complete);

	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_BCD_CTRL) & ~VIDEO_BCD_CHG_EN, AST_VIDEO_BCD_CTRL);
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) & ~(VIDEO_CAPTURE_TRIGGER | VIDEO_COMPRESS_FORCE_IDLE | VIDEO_COMPRESS_TRIGGER | VIDEO_AUTO_COMPRESS), AST_VIDEO_SEQ_CTRL);
	//If CPU is too fast, pleas read back and trigger
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_SEQ_CTRL) | VIDEO_CAPTURE_TRIGGER, AST_VIDEO_SEQ_CTRL);

	timeout = wait_for_completion_interruptible_timeout(&pAstRVAS->video_capture_complete, HZ / 2);

	if (timeout == 0)
		dev_err(pAstRVAS->pdev, "Capture timeout sts %x\n", video_read(pAstRVAS, AST_VIDEO_INT_STS));

	//dump_buffer(vem.captureBuf0.phy, 1024);
	return timeout;
}

//
// static functions
//
static u32 get_vga_mem_base(struct AstRVAS *pAstRVAS)
{
	u32 vga_mem_size, mem_size;

	mem_size = pAstRVAS->FBInfo.dwDRAMSize;
	vga_mem_size = pAstRVAS->FBInfo.dwVGASize;
	VIDEO_ENG_DBG("VGA Info : MEM Size %dMB, VGA Mem Size %dMB\n", mem_size / 1024 / 1024, vga_mem_size / 1024 / 1024);
	return (mem_size - vga_mem_size);
}

static void dump_buffer(u32 dwPhyStreamAddress, u32 size)
{
	u32 iC;
	u32 val = 0;

	for (iC = 0; iC < size; iC += 4) {
		val = readl((void *)(dwPhyStreamAddress + iC));
		VIDEO_ENG_DBG("%#x, ", val);
	}

}

static void video_set_scaling(struct AstRVAS *pAstRVAS)
{
	u32 ctrl = video_read(pAstRVAS, AST_VIDEO_CTRL);
	//no scaling
	ctrl &= ~VIDEO_CTRL_DWN_SCALING_MASK;

	VIDEO_ENG_DBG("Scaling Disable\n");
	video_write(pAstRVAS, 0x00200000, AST_VIDEO_SCALING0);
	video_write(pAstRVAS, 0x00200000, AST_VIDEO_SCALING1);
	video_write(pAstRVAS, 0x00200000, AST_VIDEO_SCALING2);
	video_write(pAstRVAS, 0x00200000, AST_VIDEO_SCALING3);

	video_write(pAstRVAS, 0x10001000, AST_VIDEO_SCAL_FACTOR);
	video_write(pAstRVAS, ctrl, AST_VIDEO_CTRL);

	video_set_Window(pAstRVAS);
}

void video_ctrl_init(struct AstRVAS *pAstRVAS)
{
	VIDEO_ENG_DBG("\n");
	VIDEO_ENG_DBG("reg address: %p\n", pAstRVAS->video_reg_base);
	video_write(pAstRVAS, (u32)vem.captureBuf0.phy, AST_VIDEO_SOURCE_BUFF0);//44h
	video_write(pAstRVAS, (u32)vem.captureBuf1.phy, AST_VIDEO_SOURCE_BUFF1);//4Ch
	video_write(pAstRVAS, (u32)vem.jpegTable.phy, AST_VIDEO_JPEG_HEADER_BUFF); //40h
	video_write(pAstRVAS, 0, AST_VIDEO_COMPRESS_READ); //3Ch

	//clr int sts
	video_write(pAstRVAS, 0xffffffff, AST_VIDEO_INT_STS);
	video_write(pAstRVAS, 0, AST_VIDEO_BCD_CTRL);

	// =============================  JPEG init ===========================================
	init_jpeg_table();
	VIDEO_ENG_DBG("JpegTable in Memory:%#x\n", vem.jpegTable.pVirt);
	dump_buffer(vem.jpegTable.phy, 80);

	// ===================================================================================
	//Specification define bit 12:13 must always 0;
	video_write(pAstRVAS, (video_read(pAstRVAS, AST_VIDEO_PASS_CTRL) &
								~(VIDEO_DUAL_EDGE_MODE | VIDEO_18BIT_SINGLE_EDGE)) |
								VIDEO_DVO_INPUT_DELAY(0x4),
								AST_VIDEO_PASS_CTRL);

	video_write(pAstRVAS, VIDEO_STREAM_PKT_N(STREAM_32_PKTS) |
					VIDEO_STREAM_PKT_SIZE(STREAM_128KB), AST_VIDEO_STREAM_SIZE);
	//rc4 init reset ..
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_CTRL) | VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);
	video_write(pAstRVAS, video_read(pAstRVAS, AST_VIDEO_CTRL) & ~VIDEO_CTRL_RC4_RST, AST_VIDEO_CTRL);

	//CRC/REDUCE_BIT register clear
	video_write(pAstRVAS, 0, AST_VIDEO_CRC1);
	video_write(pAstRVAS, 0, AST_VIDEO_CRC2);
	video_write(pAstRVAS, 0, AST_VIDEO_DATA_TRUNCA);
	video_write(pAstRVAS, 0, AST_VIDEO_COMPRESS_READ);
}

static int reserve_video_engine_memory(struct AstRVAS *pAstRVAS)
{
	u32 size;
	dma_addr_t phys_add = 0;
	void *virt_add = 0;

	memset(&vem, 0, sizeof(struct VideoEngineMem));
	vem.captureBuf0.size = VIDEO_CAPTURE_BUFFER_SIZE; //size 10M
	vem.captureBuf1.size = VIDEO_CAPTURE_BUFFER_SIZE; //size 10M
	vem.jpegTable.size =  VIDEO_JPEG_TABLE_SIZE; //size 1M

	size = vem.captureBuf0.size + vem.captureBuf1.size + vem.jpegTable.size;
	VIDEO_ENG_DBG("Allocating memory size: 0x%x\n", size);
	virt_add = dma_alloc_coherent(pAstRVAS->pdev, size, &phys_add,
				      GFP_KERNEL);

	if (!virt_add) {
		pr_err("Cannot alloc buffer for video engine\n");
		return -ENOMEM;
	}

	vem.captureBuf0.phy =  phys_add;
	vem.captureBuf1.phy =  phys_add + vem.captureBuf0.size;
	vem.jpegTable.phy = phys_add + vem.captureBuf0.size + vem.captureBuf1.size;

	vem.captureBuf0.pVirt = (void *)virt_add;
	vem.captureBuf1.pVirt = (void *)(virt_add + vem.captureBuf0.size);
	vem.jpegTable.pVirt = (void *)(virt_add + vem.captureBuf0.size + vem.captureBuf1.size);

	VIDEO_ENG_DBG("Allocated: phys: 0x%llx\n", phys_add);
	VIDEO_ENG_DBG("Phy: Buf0:0x%llx; Buf1:0x%llx; jpegT:0x%llx\n", vem.captureBuf0.phy, vem.captureBuf1.phy, vem.jpegTable.phy);
	VIDEO_ENG_DBG("Virt: Buf0:%p; Buf1:%p; JpegT:%p\n", vem.captureBuf0.pVirt, vem.captureBuf1.pVirt, vem.jpegTable.pVirt);

	return 0;
}

/************************************************ JPEG ***************************************************************************************/
static void init_jpeg_table(void)
{
	int i = 0;
	int base = 0;
	u32 *tlb_table = vem.jpegTable.pVirt;

	//JPEG header default value:
	for (i = 0; i < 12; i++) {
		base = (256 * i);
		tlb_table[base + 0] = 0xE0FFD8FF;
		tlb_table[base + 1] = 0x464A1000;
		tlb_table[base + 2] = 0x01004649;
		tlb_table[base + 3] = 0x60000101;
		tlb_table[base + 4] = 0x00006000;
		tlb_table[base + 5] = 0x0F00FEFF;
		tlb_table[base + 6] = 0x00002D05;
		tlb_table[base + 7] = 0x00000000;
		tlb_table[base + 8] = 0x00000000;
		tlb_table[base + 9] = 0x00DBFF00;
		tlb_table[base + 44] = 0x081100C0;
		tlb_table[base + 45] = 0x00000000;
		tlb_table[base + 47] = 0x03011102;
		tlb_table[base + 48] = 0xC4FF0111;
		tlb_table[base + 49] = 0x00001F00;
		tlb_table[base + 50] = 0x01010501;
		tlb_table[base + 51] = 0x01010101;
		tlb_table[base + 52] = 0x00000000;
		tlb_table[base + 53] = 0x00000000;
		tlb_table[base + 54] = 0x04030201;
		tlb_table[base + 55] = 0x08070605;
		tlb_table[base + 56] = 0xFF0B0A09;
		tlb_table[base + 57] = 0x10B500C4;
		tlb_table[base + 58] = 0x03010200;
		tlb_table[base + 59] = 0x03040203;
		tlb_table[base + 60] = 0x04040505;
		tlb_table[base + 61] = 0x7D010000;
		tlb_table[base + 62] = 0x00030201;
		tlb_table[base + 63] = 0x12051104;
		tlb_table[base + 64] = 0x06413121;
		tlb_table[base + 65] = 0x07615113;
		tlb_table[base + 66] = 0x32147122;
		tlb_table[base + 67] = 0x08A19181;
		tlb_table[base + 68] = 0xC1B14223;
		tlb_table[base + 69] = 0xF0D15215;
		tlb_table[base + 70] = 0x72623324;
		tlb_table[base + 71] = 0x160A0982;
		tlb_table[base + 72] = 0x1A191817;
		tlb_table[base + 73] = 0x28272625;
		tlb_table[base + 74] = 0x35342A29;
		tlb_table[base + 75] = 0x39383736;
		tlb_table[base + 76] = 0x4544433A;
		tlb_table[base + 77] = 0x49484746;
		tlb_table[base + 78] = 0x5554534A;
		tlb_table[base + 79] = 0x59585756;
		tlb_table[base + 80] = 0x6564635A;
		tlb_table[base + 81] = 0x69686766;
		tlb_table[base + 82] = 0x7574736A;
		tlb_table[base + 83] = 0x79787776;
		tlb_table[base + 84] = 0x8584837A;
		tlb_table[base + 85] = 0x89888786;
		tlb_table[base + 86] = 0x9493928A;
		tlb_table[base + 87] = 0x98979695;
		tlb_table[base + 88] = 0xA3A29A99;
		tlb_table[base + 89] = 0xA7A6A5A4;
		tlb_table[base + 90] = 0xB2AAA9A8;
		tlb_table[base + 91] = 0xB6B5B4B3;
		tlb_table[base + 92] = 0xBAB9B8B7;
		tlb_table[base + 93] = 0xC5C4C3C2;
		tlb_table[base + 94] = 0xC9C8C7C6;
		tlb_table[base + 95] = 0xD4D3D2CA;
		tlb_table[base + 96] = 0xD8D7D6D5;
		tlb_table[base + 97] = 0xE2E1DAD9;
		tlb_table[base + 98] = 0xE6E5E4E3;
		tlb_table[base + 99] = 0xEAE9E8E7;
		tlb_table[base + 100] = 0xF4F3F2F1;
		tlb_table[base + 101] = 0xF8F7F6F5;
		tlb_table[base + 102] = 0xC4FFFAF9;
		tlb_table[base + 103] = 0x00011F00;
		tlb_table[base + 104] = 0x01010103;
		tlb_table[base + 105] = 0x01010101;
		tlb_table[base + 106] = 0x00000101;
		tlb_table[base + 107] = 0x00000000;
		tlb_table[base + 108] = 0x04030201;
		tlb_table[base + 109] = 0x08070605;
		tlb_table[base + 110] = 0xFF0B0A09;
		tlb_table[base + 111] = 0x11B500C4;
		tlb_table[base + 112] = 0x02010200;
		tlb_table[base + 113] = 0x04030404;
		tlb_table[base + 114] = 0x04040507;
		tlb_table[base + 115] = 0x77020100;
		tlb_table[base + 116] = 0x03020100;
		tlb_table[base + 117] = 0x21050411;
		tlb_table[base + 118] = 0x41120631;
		tlb_table[base + 119] = 0x71610751;
		tlb_table[base + 120] = 0x81322213;
		tlb_table[base + 121] = 0x91421408;
		tlb_table[base + 122] = 0x09C1B1A1;
		tlb_table[base + 123] = 0xF0523323;
		tlb_table[base + 124] = 0xD1726215;
		tlb_table[base + 125] = 0x3424160A;
		tlb_table[base + 126] = 0x17F125E1;
		tlb_table[base + 127] = 0x261A1918;
		tlb_table[base + 128] = 0x2A292827;
		tlb_table[base + 129] = 0x38373635;
		tlb_table[base + 130] = 0x44433A39;
		tlb_table[base + 131] = 0x48474645;
		tlb_table[base + 132] = 0x54534A49;
		tlb_table[base + 133] = 0x58575655;
		tlb_table[base + 134] = 0x64635A59;
		tlb_table[base + 135] = 0x68676665;
		tlb_table[base + 136] = 0x74736A69;
		tlb_table[base + 137] = 0x78777675;
		tlb_table[base + 138] = 0x83827A79;
		tlb_table[base + 139] = 0x87868584;
		tlb_table[base + 140] = 0x928A8988;
		tlb_table[base + 141] = 0x96959493;
		tlb_table[base + 142] = 0x9A999897;
		tlb_table[base + 143] = 0xA5A4A3A2;
		tlb_table[base + 144] = 0xA9A8A7A6;
		tlb_table[base + 145] = 0xB4B3B2AA;
		tlb_table[base + 146] = 0xB8B7B6B5;
		tlb_table[base + 147] = 0xC3C2BAB9;
		tlb_table[base + 148] = 0xC7C6C5C4;
		tlb_table[base + 149] = 0xD2CAC9C8;
		tlb_table[base + 150] = 0xD6D5D4D3;
		tlb_table[base + 151] = 0xDAD9D8D7;
		tlb_table[base + 152] = 0xE5E4E3E2;
		tlb_table[base + 153] = 0xE9E8E7E6;
		tlb_table[base + 154] = 0xF4F3F2EA;
		tlb_table[base + 155] = 0xF8F7F6F5;
		tlb_table[base + 156] = 0xDAFFFAF9;
		tlb_table[base + 157] = 0x01030C00;
		tlb_table[base + 158] = 0x03110200;
		tlb_table[base + 159] = 0x003F0011;

		//Table 0
		if (i == 0) {
			tlb_table[base + 10] = 0x0D140043;
			tlb_table[base + 11] = 0x0C0F110F;
			tlb_table[base + 12] = 0x11101114;
			tlb_table[base + 13] = 0x17141516;
			tlb_table[base + 14] = 0x1E20321E;
			tlb_table[base + 15] = 0x3D1E1B1B;
			tlb_table[base + 16] = 0x32242E2B;
			tlb_table[base + 17] = 0x4B4C3F48;
			tlb_table[base + 18] = 0x44463F47;
			tlb_table[base + 19] = 0x61735A50;
			tlb_table[base + 20] = 0x566C5550;
			tlb_table[base + 21] = 0x88644644;
			tlb_table[base + 22] = 0x7A766C65;
			tlb_table[base + 23] = 0x4D808280;
			tlb_table[base + 24] = 0x8C978D60;
			tlb_table[base + 25] = 0x7E73967D;
			tlb_table[base + 26] = 0xDBFF7B80;
			tlb_table[base + 27] = 0x1F014300;
			tlb_table[base + 28] = 0x272D2121;
			tlb_table[base + 29] = 0x3030582D;
			tlb_table[base + 30] = 0x697BB958;
			tlb_table[base + 31] = 0xB8B9B97B;
			tlb_table[base + 32] = 0xB9B8A6A6;
			tlb_table[base + 33] = 0xB9B9B9B9;
			tlb_table[base + 34] = 0xB9B9B9B9;
			tlb_table[base + 35] = 0xB9B9B9B9;
			tlb_table[base + 36] = 0xB9B9B9B9;
			tlb_table[base + 37] = 0xB9B9B9B9;
			tlb_table[base + 38] = 0xB9B9B9B9;
			tlb_table[base + 39] = 0xB9B9B9B9;
			tlb_table[base + 40] = 0xB9B9B9B9;
			tlb_table[base + 41] = 0xB9B9B9B9;
			tlb_table[base + 42] = 0xB9B9B9B9;
			tlb_table[base + 43] = 0xFFB9B9B9;
		}
		//Table 1
		if (i == 1) {
			tlb_table[base + 10] = 0x0C110043;
			tlb_table[base + 11] = 0x0A0D0F0D;
			tlb_table[base + 12] = 0x0F0E0F11;
			tlb_table[base + 13] = 0x14111213;
			tlb_table[base + 14] = 0x1A1C2B1A;
			tlb_table[base + 15] = 0x351A1818;
			tlb_table[base + 16] = 0x2B1F2826;
			tlb_table[base + 17] = 0x4142373F;
			tlb_table[base + 18] = 0x3C3D373E;
			tlb_table[base + 19] = 0x55644E46;
			tlb_table[base + 20] = 0x4B5F4A46;
			tlb_table[base + 21] = 0x77573D3C;
			tlb_table[base + 22] = 0x6B675F58;
			tlb_table[base + 23] = 0x43707170;
			tlb_table[base + 24] = 0x7A847B54;
			tlb_table[base + 25] = 0x6E64836D;
			tlb_table[base + 26] = 0xDBFF6C70;
			tlb_table[base + 27] = 0x1B014300;
			tlb_table[base + 28] = 0x22271D1D;
			tlb_table[base + 29] = 0x2A2A4C27;
			tlb_table[base + 30] = 0x5B6BA04C;
			tlb_table[base + 31] = 0xA0A0A06B;
			tlb_table[base + 32] = 0xA0A0A0A0;
			tlb_table[base + 33] = 0xA0A0A0A0;
			tlb_table[base + 34] = 0xA0A0A0A0;
			tlb_table[base + 35] = 0xA0A0A0A0;
			tlb_table[base + 36] = 0xA0A0A0A0;
			tlb_table[base + 37] = 0xA0A0A0A0;
			tlb_table[base + 38] = 0xA0A0A0A0;
			tlb_table[base + 39] = 0xA0A0A0A0;
			tlb_table[base + 40] = 0xA0A0A0A0;
			tlb_table[base + 41] = 0xA0A0A0A0;
			tlb_table[base + 42] = 0xA0A0A0A0;
			tlb_table[base + 43] = 0xFFA0A0A0;
		}
		//Table 2
		if (i == 2) {
			tlb_table[base + 10] = 0x090E0043;
			tlb_table[base + 11] = 0x090A0C0A;
			tlb_table[base + 12] = 0x0C0B0C0E;
			tlb_table[base + 13] = 0x110E0F10;
			tlb_table[base + 14] = 0x15172415;
			tlb_table[base + 15] = 0x2C151313;
			tlb_table[base + 16] = 0x241A211F;
			tlb_table[base + 17] = 0x36372E34;
			tlb_table[base + 18] = 0x31322E33;
			tlb_table[base + 19] = 0x4653413A;
			tlb_table[base + 20] = 0x3E4E3D3A;
			tlb_table[base + 21] = 0x62483231;
			tlb_table[base + 22] = 0x58564E49;
			tlb_table[base + 23] = 0x385D5E5D;
			tlb_table[base + 24] = 0x656D6645;
			tlb_table[base + 25] = 0x5B536C5A;
			tlb_table[base + 26] = 0xDBFF595D;
			tlb_table[base + 27] = 0x16014300;
			tlb_table[base + 28] = 0x1C201818;
			tlb_table[base + 29] = 0x22223F20;
			tlb_table[base + 30] = 0x4B58853F;
			tlb_table[base + 31] = 0x85858558;
			tlb_table[base + 32] = 0x85858585;
			tlb_table[base + 33] = 0x85858585;
			tlb_table[base + 34] = 0x85858585;
			tlb_table[base + 35] = 0x85858585;
			tlb_table[base + 36] = 0x85858585;
			tlb_table[base + 37] = 0x85858585;
			tlb_table[base + 38] = 0x85858585;
			tlb_table[base + 39] = 0x85858585;
			tlb_table[base + 40] = 0x85858585;
			tlb_table[base + 41] = 0x85858585;
			tlb_table[base + 42] = 0x85858585;
			tlb_table[base + 43] = 0xFF858585;
		}
		//Table 3
		if (i == 3) {
			tlb_table[base + 10] = 0x070B0043;
			tlb_table[base + 11] = 0x07080A08;
			tlb_table[base + 12] = 0x0A090A0B;
			tlb_table[base + 13] = 0x0D0B0C0C;
			tlb_table[base + 14] = 0x11121C11;
			tlb_table[base + 15] = 0x23110F0F;
			tlb_table[base + 16] = 0x1C141A19;
			tlb_table[base + 17] = 0x2B2B2429;
			tlb_table[base + 18] = 0x27282428;
			tlb_table[base + 19] = 0x3842332E;
			tlb_table[base + 20] = 0x313E302E;
			tlb_table[base + 21] = 0x4E392827;
			tlb_table[base + 22] = 0x46443E3A;
			tlb_table[base + 23] = 0x2C4A4A4A;
			tlb_table[base + 24] = 0x50565137;
			tlb_table[base + 25] = 0x48425647;
			tlb_table[base + 26] = 0xDBFF474A;
			tlb_table[base + 27] = 0x12014300;
			tlb_table[base + 28] = 0x161A1313;
			tlb_table[base + 29] = 0x1C1C331A;
			tlb_table[base + 30] = 0x3D486C33;
			tlb_table[base + 31] = 0x6C6C6C48;
			tlb_table[base + 32] = 0x6C6C6C6C;
			tlb_table[base + 33] = 0x6C6C6C6C;
			tlb_table[base + 34] = 0x6C6C6C6C;
			tlb_table[base + 35] = 0x6C6C6C6C;
			tlb_table[base + 36] = 0x6C6C6C6C;
			tlb_table[base + 37] = 0x6C6C6C6C;
			tlb_table[base + 38] = 0x6C6C6C6C;
			tlb_table[base + 39] = 0x6C6C6C6C;
			tlb_table[base + 40] = 0x6C6C6C6C;
			tlb_table[base + 41] = 0x6C6C6C6C;
			tlb_table[base + 42] = 0x6C6C6C6C;
			tlb_table[base + 43] = 0xFF6C6C6C;
		}
		//Table 4
		if (i == 4) {
			tlb_table[base + 10] = 0x06090043;
			tlb_table[base + 11] = 0x05060706;
			tlb_table[base + 12] = 0x07070709;
			tlb_table[base + 13] = 0x0A09090A;
			tlb_table[base + 14] = 0x0D0E160D;
			tlb_table[base + 15] = 0x1B0D0C0C;
			tlb_table[base + 16] = 0x16101413;
			tlb_table[base + 17] = 0x21221C20;
			tlb_table[base + 18] = 0x1E1F1C20;
			tlb_table[base + 19] = 0x2B332824;
			tlb_table[base + 20] = 0x26302624;
			tlb_table[base + 21] = 0x3D2D1F1E;
			tlb_table[base + 22] = 0x3735302D;
			tlb_table[base + 23] = 0x22393A39;
			tlb_table[base + 24] = 0x3F443F2B;
			tlb_table[base + 25] = 0x38334338;
			tlb_table[base + 26] = 0xDBFF3739;
			tlb_table[base + 27] = 0x0D014300;
			tlb_table[base + 28] = 0x11130E0E;
			tlb_table[base + 29] = 0x15152613;
			tlb_table[base + 30] = 0x2D355026;
			tlb_table[base + 31] = 0x50505035;
			tlb_table[base + 32] = 0x50505050;
			tlb_table[base + 33] = 0x50505050;
			tlb_table[base + 34] = 0x50505050;
			tlb_table[base + 35] = 0x50505050;
			tlb_table[base + 36] = 0x50505050;
			tlb_table[base + 37] = 0x50505050;
			tlb_table[base + 38] = 0x50505050;
			tlb_table[base + 39] = 0x50505050;
			tlb_table[base + 40] = 0x50505050;
			tlb_table[base + 41] = 0x50505050;
			tlb_table[base + 42] = 0x50505050;
			tlb_table[base + 43] = 0xFF505050;
		}
		//Table 5
		if (i == 5) {
			tlb_table[base + 10] = 0x04060043;
			tlb_table[base + 11] = 0x03040504;
			tlb_table[base + 12] = 0x05040506;
			tlb_table[base + 13] = 0x07060606;
			tlb_table[base + 14] = 0x09090F09;
			tlb_table[base + 15] = 0x12090808;
			tlb_table[base + 16] = 0x0F0A0D0D;
			tlb_table[base + 17] = 0x16161315;
			tlb_table[base + 18] = 0x14151315;
			tlb_table[base + 19] = 0x1D221B18;
			tlb_table[base + 20] = 0x19201918;
			tlb_table[base + 21] = 0x281E1514;
			tlb_table[base + 22] = 0x2423201E;
			tlb_table[base + 23] = 0x17262726;
			tlb_table[base + 24] = 0x2A2D2A1C;
			tlb_table[base + 25] = 0x25222D25;
			tlb_table[base + 26] = 0xDBFF2526;
			tlb_table[base + 27] = 0x09014300;
			tlb_table[base + 28] = 0x0B0D0A0A;
			tlb_table[base + 29] = 0x0E0E1A0D;
			tlb_table[base + 30] = 0x1F25371A;
			tlb_table[base + 31] = 0x37373725;
			tlb_table[base + 32] = 0x37373737;
			tlb_table[base + 33] = 0x37373737;
			tlb_table[base + 34] = 0x37373737;
			tlb_table[base + 35] = 0x37373737;
			tlb_table[base + 36] = 0x37373737;
			tlb_table[base + 37] = 0x37373737;
			tlb_table[base + 38] = 0x37373737;
			tlb_table[base + 39] = 0x37373737;
			tlb_table[base + 40] = 0x37373737;
			tlb_table[base + 41] = 0x37373737;
			tlb_table[base + 42] = 0x37373737;
			tlb_table[base + 43] = 0xFF373737;
		}
		//Table 6
		if (i == 6) {
			tlb_table[base + 10] = 0x02030043;
			tlb_table[base + 11] = 0x01020202;
			tlb_table[base + 12] = 0x02020203;
			tlb_table[base + 13] = 0x03030303;
			tlb_table[base + 14] = 0x04040704;
			tlb_table[base + 15] = 0x09040404;
			tlb_table[base + 16] = 0x07050606;
			tlb_table[base + 17] = 0x0B0B090A;
			tlb_table[base + 18] = 0x0A0A090A;
			tlb_table[base + 19] = 0x0E110D0C;
			tlb_table[base + 20] = 0x0C100C0C;
			tlb_table[base + 21] = 0x140F0A0A;
			tlb_table[base + 22] = 0x1211100F;
			tlb_table[base + 23] = 0x0B131313;
			tlb_table[base + 24] = 0x1516150E;
			tlb_table[base + 25] = 0x12111612;
			tlb_table[base + 26] = 0xDBFF1213;
			tlb_table[base + 27] = 0x04014300;
			tlb_table[base + 28] = 0x05060505;
			tlb_table[base + 29] = 0x07070D06;
			tlb_table[base + 30] = 0x0F121B0D;
			tlb_table[base + 31] = 0x1B1B1B12;
			tlb_table[base + 32] = 0x1B1B1B1B;
			tlb_table[base + 33] = 0x1B1B1B1B;
			tlb_table[base + 34] = 0x1B1B1B1B;
			tlb_table[base + 35] = 0x1B1B1B1B;
			tlb_table[base + 36] = 0x1B1B1B1B;
			tlb_table[base + 37] = 0x1B1B1B1B;
			tlb_table[base + 38] = 0x1B1B1B1B;
			tlb_table[base + 39] = 0x1B1B1B1B;
			tlb_table[base + 40] = 0x1B1B1B1B;
			tlb_table[base + 41] = 0x1B1B1B1B;
			tlb_table[base + 42] = 0x1B1B1B1B;
			tlb_table[base + 43] = 0xFF1B1B1B;
		}
		//Table 7
		if (i == 7) {
			tlb_table[base + 10] = 0x01020043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010102;
			tlb_table[base + 13] = 0x02020202;
			tlb_table[base + 14] = 0x03030503;
			tlb_table[base + 15] = 0x06030202;
			tlb_table[base + 16] = 0x05030404;
			tlb_table[base + 17] = 0x07070607;
			tlb_table[base + 18] = 0x06070607;
			tlb_table[base + 19] = 0x090B0908;
			tlb_table[base + 20] = 0x080A0808;
			tlb_table[base + 21] = 0x0D0A0706;
			tlb_table[base + 22] = 0x0C0B0A0A;
			tlb_table[base + 23] = 0x070C0D0C;
			tlb_table[base + 24] = 0x0E0F0E09;
			tlb_table[base + 25] = 0x0C0B0F0C;
			tlb_table[base + 26] = 0xDBFF0C0C;
			tlb_table[base + 27] = 0x03014300;
			tlb_table[base + 28] = 0x03040303;
			tlb_table[base + 29] = 0x04040804;
			tlb_table[base + 30] = 0x0A0C1208;
			tlb_table[base + 31] = 0x1212120C;
			tlb_table[base + 32] = 0x12121212;
			tlb_table[base + 33] = 0x12121212;
			tlb_table[base + 34] = 0x12121212;
			tlb_table[base + 35] = 0x12121212;
			tlb_table[base + 36] = 0x12121212;
			tlb_table[base + 37] = 0x12121212;
			tlb_table[base + 38] = 0x12121212;
			tlb_table[base + 39] = 0x12121212;
			tlb_table[base + 40] = 0x12121212;
			tlb_table[base + 41] = 0x12121212;
			tlb_table[base + 42] = 0x12121212;
			tlb_table[base + 43] = 0xFF121212;
		}
		//Table 8
		if (i == 8) {
			tlb_table[base + 10] = 0x01020043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010102;
			tlb_table[base + 13] = 0x02020202;
			tlb_table[base + 14] = 0x03030503;
			tlb_table[base + 15] = 0x06030202;
			tlb_table[base + 16] = 0x05030404;
			tlb_table[base + 17] = 0x07070607;
			tlb_table[base + 18] = 0x06070607;
			tlb_table[base + 19] = 0x090B0908;
			tlb_table[base + 20] = 0x080A0808;
			tlb_table[base + 21] = 0x0D0A0706;
			tlb_table[base + 22] = 0x0C0B0A0A;
			tlb_table[base + 23] = 0x070C0D0C;
			tlb_table[base + 24] = 0x0E0F0E09;
			tlb_table[base + 25] = 0x0C0B0F0C;
			tlb_table[base + 26] = 0xDBFF0C0C;
			tlb_table[base + 27] = 0x02014300;
			tlb_table[base + 28] = 0x03030202;
			tlb_table[base + 29] = 0x04040703;
			tlb_table[base + 30] = 0x080A0F07;
			tlb_table[base + 31] = 0x0F0F0F0A;
			tlb_table[base + 32] = 0x0F0F0F0F;
			tlb_table[base + 33] = 0x0F0F0F0F;
			tlb_table[base + 34] = 0x0F0F0F0F;
			tlb_table[base + 35] = 0x0F0F0F0F;
			tlb_table[base + 36] = 0x0F0F0F0F;
			tlb_table[base + 37] = 0x0F0F0F0F;
			tlb_table[base + 38] = 0x0F0F0F0F;
			tlb_table[base + 39] = 0x0F0F0F0F;
			tlb_table[base + 40] = 0x0F0F0F0F;
			tlb_table[base + 41] = 0x0F0F0F0F;
			tlb_table[base + 42] = 0x0F0F0F0F;
			tlb_table[base + 43] = 0xFF0F0F0F;
		}
		//Table 9
		if (i == 9) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x02020302;
			tlb_table[base + 15] = 0x04020202;
			tlb_table[base + 16] = 0x03020303;
			tlb_table[base + 17] = 0x05050405;
			tlb_table[base + 18] = 0x05050405;
			tlb_table[base + 19] = 0x07080606;
			tlb_table[base + 20] = 0x06080606;
			tlb_table[base + 21] = 0x0A070505;
			tlb_table[base + 22] = 0x09080807;
			tlb_table[base + 23] = 0x05090909;
			tlb_table[base + 24] = 0x0A0B0A07;
			tlb_table[base + 25] = 0x09080B09;
			tlb_table[base + 26] = 0xDBFF0909;
			tlb_table[base + 27] = 0x02014300;
			tlb_table[base + 28] = 0x02030202;
			tlb_table[base + 29] = 0x03030503;
			tlb_table[base + 30] = 0x07080C05;
			tlb_table[base + 31] = 0x0C0C0C08;
			tlb_table[base + 32] = 0x0C0C0C0C;
			tlb_table[base + 33] = 0x0C0C0C0C;
			tlb_table[base + 34] = 0x0C0C0C0C;
			tlb_table[base + 35] = 0x0C0C0C0C;
			tlb_table[base + 36] = 0x0C0C0C0C;
			tlb_table[base + 37] = 0x0C0C0C0C;
			tlb_table[base + 38] = 0x0C0C0C0C;
			tlb_table[base + 39] = 0x0C0C0C0C;
			tlb_table[base + 40] = 0x0C0C0C0C;
			tlb_table[base + 41] = 0x0C0C0C0C;
			tlb_table[base + 42] = 0x0C0C0C0C;
			tlb_table[base + 43] = 0xFF0C0C0C;
		}
		//Table 10
		if (i == 10) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x01010201;
			tlb_table[base + 15] = 0x03010101;
			tlb_table[base + 16] = 0x02010202;
			tlb_table[base + 17] = 0x03030303;
			tlb_table[base + 18] = 0x03030303;
			tlb_table[base + 19] = 0x04050404;
			tlb_table[base + 20] = 0x04050404;
			tlb_table[base + 21] = 0x06050303;
			tlb_table[base + 22] = 0x06050505;
			tlb_table[base + 23] = 0x03060606;
			tlb_table[base + 24] = 0x07070704;
			tlb_table[base + 25] = 0x06050706;
			tlb_table[base + 26] = 0xDBFF0606;
			tlb_table[base + 27] = 0x01014300;
			tlb_table[base + 28] = 0x01020101;
			tlb_table[base + 29] = 0x02020402;
			tlb_table[base + 30] = 0x05060904;
			tlb_table[base + 31] = 0x09090906;
			tlb_table[base + 32] = 0x09090909;
			tlb_table[base + 33] = 0x09090909;
			tlb_table[base + 34] = 0x09090909;
			tlb_table[base + 35] = 0x09090909;
			tlb_table[base + 36] = 0x09090909;
			tlb_table[base + 37] = 0x09090909;
			tlb_table[base + 38] = 0x09090909;
			tlb_table[base + 39] = 0x09090909;
			tlb_table[base + 40] = 0x09090909;
			tlb_table[base + 41] = 0x09090909;
			tlb_table[base + 42] = 0x09090909;
			tlb_table[base + 43] = 0xFF090909;
		}
		//Table 11
		if (i == 11) {
			tlb_table[base + 10] = 0x01010043;
			tlb_table[base + 11] = 0x01010101;
			tlb_table[base + 12] = 0x01010101;
			tlb_table[base + 13] = 0x01010101;
			tlb_table[base + 14] = 0x01010101;
			tlb_table[base + 15] = 0x01010101;
			tlb_table[base + 16] = 0x01010101;
			tlb_table[base + 17] = 0x01010101;
			tlb_table[base + 18] = 0x01010101;
			tlb_table[base + 19] = 0x02020202;
			tlb_table[base + 20] = 0x02020202;
			tlb_table[base + 21] = 0x03020101;
			tlb_table[base + 22] = 0x03020202;
			tlb_table[base + 23] = 0x01030303;
			tlb_table[base + 24] = 0x03030302;
			tlb_table[base + 25] = 0x03020303;
			tlb_table[base + 26] = 0xDBFF0403;
			tlb_table[base + 27] = 0x01014300;
			tlb_table[base + 28] = 0x01010101;
			tlb_table[base + 29] = 0x01010201;
			tlb_table[base + 30] = 0x03040602;
			tlb_table[base + 31] = 0x06060604;
			tlb_table[base + 32] = 0x06060606;
			tlb_table[base + 33] = 0x06060606;
			tlb_table[base + 34] = 0x06060606;
			tlb_table[base + 35] = 0x06060606;
			tlb_table[base + 36] = 0x06060606;
			tlb_table[base + 37] = 0x06060606;
			tlb_table[base + 38] = 0x06060606;
			tlb_table[base + 39] = 0x06060606;
			tlb_table[base + 40] = 0x06060606;
			tlb_table[base + 41] = 0x06060606;
			tlb_table[base + 42] = 0x06060606;
			tlb_table[base + 43] = 0xFF060606;
		}
	}
}

static inline void
video_write(struct AstRVAS *pAstRVAS, u32 val, u32 reg)
{
	VIDEO_ENG_DBG("write offset: %x, val: %x\n", reg, val);
	//Video is lock after reset, need always unlock
	//unlock
	writel(VIDEO_PROTECT_UNLOCK, pAstRVAS->video_reg_base);
	writel(val, pAstRVAS->video_reg_base + reg);

}

static inline u32
video_read(struct AstRVAS *pAstRVAS, u32 reg)
{
	u32 val = readl(pAstRVAS->video_reg_base + reg);

	VIDEO_ENG_DBG("read offset: %x, val: %x\n", reg, val);
	return val;
}

