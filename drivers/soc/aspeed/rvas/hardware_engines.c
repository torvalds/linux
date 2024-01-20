// SPDX-License-Identifier: GPL-2.0+
/*
 * File Name     : hardware_engines.c
 * Description   : AST2600 frame grabber hardware engines
 *
 * Copyright (C) 2019-2021 ASPEED Technology Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <linux/time.h>

#include "hardware_engines.h"
#include "video.h"
#include "video_debug.h"

static u32 dwBucketSizeRegOffset[BSE_MAX_BUCKET_SIZE_REGS] = { 0x20, 0x24, 0x28,
	0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58,
	0x5c };
static u32 arrBuckSizeRegIndex[16] = { 3, 5, 8, 6, 1, 7, 11, 10, 14, 13, 2, 4,
	9, 12, 0, 15 };

static struct Resolution resTable1[0x3B - 0x30 + 1] = { { 800, 600 }, { 1024, 768 }, {
	1280, 1024 }, { 1600, 1200 }, { 1920, 1200 }, { 1280, 800 },
	{ 1440, 900 }, { 1680, 1050 }, { 1920, 1080 }, { 1366, 768 }, { 1600,
		900 }, { 1152, 864 }, };

static struct Resolution resTable2[0x52 - 0x50 + 1] = { { 320, 240 }, { 400, 300 }, {
	512, 384 }, };

static void prepare_bse_descriptor_2(struct Descriptor *pDAddress,
				     u32 dwSourceAddress,
				     u32 dwDestAddress,
				     bool bNotLastEntry,
				     u16 wStride,
				     u8 bytesPerPixel,
				     u32 dwFetchWidthPixels,
				     u32 dwFetchHeight,
				     bool bInterrupt,
				     u8 byBuckSizeRegIndex);

static struct BSEAggregateRegister set_up_bse_bucket_2(struct AstRVAS *pAstRVAS,
						       u8 *abyBitIndexes,
						       u8 byTotalBucketCount,
						       u8 byBSBytesPerPixel,
						       u32 dwFetchWidthPixels,
						       u32 dwFetchHeight,
						       u32 dwBucketSizeIndex);

static inline u32 ast_video_read(u32 video_reg_base, u32 reg)
{
	u32 val = readl((void *)(video_reg_base + reg));
	return val;
}

// Get color depth
static void ast_video_get_color_mode(u8 byNewColorMode, struct VideoGeometry *pvg)
{
	switch (byNewColorMode) {
	case MODE_EGA:
		pvg->gmt = VGAGraphicsMode; //4pp mode12/mode6A
		pvg->byBitsPerPixel = 4;
		break;

	case MODE_VGA:
		pvg->gmt = VGAGraphicsMode; //mode 13
		pvg->byBitsPerPixel = 8;
		break;

	case MODE_BPP16:
		pvg->gmt = AGAGraphicsMode;
		pvg->byBitsPerPixel = 16;
		break;

	case MODE_BPP32:
		pvg->gmt = AGAGraphicsMode;
		pvg->byBitsPerPixel = 32;
		break;

	case MODE_TEXT:
		pvg->gmt = TextMode;
		pvg->byBitsPerPixel = 0;
		break;

	case MODE_CGA:
		break;

	default:
		pvg->byBitsPerPixel = 8;
		break;
	}
}

//Mode ID mapping - use ID as index to the resolution table
static void ast_video_get_indexed_mode(struct ModeInfo *pModeInfo, struct VideoGeometry *pvg)
{
	u8 byModeIndex = (pModeInfo->byModeID & 0xf0);

	VIDEO_DBG("Mode ID %#x\n", pModeInfo->byModeID);
	pvg->byModeID = pModeInfo->byModeID;

	if (pModeInfo->byModeID == 0x12) {
		pvg->wScreenWidth = 640;
		pvg->wScreenHeight = 480;
	} else if (byModeIndex == 0x20) {
		pvg->wScreenWidth = 640;
		pvg->wScreenHeight = 480;
	} else if (byModeIndex == 0x30) {
		pvg->wScreenWidth =
			resTable1[pModeInfo->byModeID & 0x0f].wWidth;
		pvg->wScreenHeight =
			resTable1[pModeInfo->byModeID & 0x0f].wHeight;
	} else if (byModeIndex == 0x50) {
		pvg->wScreenWidth =
			resTable2[pModeInfo->byModeID & 0x03].wWidth;
		pvg->wScreenHeight =
			resTable2[pModeInfo->byModeID & 0x03].wHeight;
	} else if (byModeIndex == 0x60) {
		pvg->wScreenWidth = 800;
		pvg->wScreenHeight = 600;
	} else {
		VIDEO_DBG("Mode ID %#x\n", pModeInfo->byModeID);
		pvg->wScreenWidth = 0;
		pvg->wScreenHeight = 0;
	}
}

//check special modes
static void ast_video_set_special_modes(struct ModeInfo *pModeInfo, struct AstRVAS *pAstRVAS)
{
	u8 byVGACR1 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x1)); //number of chars per line
	u8 byVGACR7 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x7));
	u8 byVGACR12 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x12));
	u8 byVGASR1 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_SEQ_OFFSET + 0x1));
	struct VideoGeometry *pvg = &pAstRVAS->current_vg;
	u32 dwHorizontalDisplayEnd = 0;
	u32 dwVerticalDisplayEnd = 0;

	dwHorizontalDisplayEnd = (byVGACR1 + 1) << 3;
	dwVerticalDisplayEnd = (((byVGACR7 & 0x40) << 3)
		| ((byVGACR7 & 0x2) << 7) | byVGACR12) + 1;

	VIDEO_DBG("byVGACR1=0x%x,byVGACR7=0x%x,byVGACR12=0x%x\n", byVGACR1,
		  byVGACR7, byVGACR12);
	VIDEO_DBG("Mode ID %#x, dwHorizontalDisplayEnd 0x%x, dwVerticalDisplayEnd 0x%x\n",
		  pModeInfo->byModeID, dwHorizontalDisplayEnd,
		  dwVerticalDisplayEnd);

	// set up special mode
	if (VGAGraphicsMode == pvg->gmt && pvg->byBitsPerPixel == 8) { // mode 13
		pvg->wScreenHeight = 200;
		pvg->wScreenWidth = 320;
		pvg->wStride = 320;
	} else if (TextMode == pvg->gmt) { // text mode
		pvg->wScreenHeight = dwVerticalDisplayEnd;
		pvg->wScreenWidth = dwHorizontalDisplayEnd;

		if (!(byVGASR1 & 0x1))
			pvg->wScreenWidth += (byVGACR1 + 1);

		pvg->wStride = pvg->wScreenWidth;
	} else if (pvg->byBitsPerPixel == 4) {
		pvg->wStride = pvg->wScreenWidth;
	}
}

static u32 ast_video_get_pitch(struct AstRVAS *pAstRVAS)
{
	u32 dwPitch = 0;
	u8 byVGACR13 = 0;
	u8 byVGACR14 = 0;
	u8 byVGACR17 = 0;
	u16 wOffsetUpper = 0;
	u16 wOffset = 0;
	struct VideoGeometry *pvg = &pAstRVAS->current_vg;

	//read actual register
	byVGACR13 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x13));
	byVGACR14 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x14));
	byVGACR17 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC_OFFSET + 0x17));
	wOffsetUpper = readb((void *)(pAstRVAS->grce_reg_base + 0xb0));

	wOffset = (wOffsetUpper << 8) | byVGACR13;
	VIDEO_DBG("wOffsetUpper= %#x, byVGACR13= %#x,
		  byVGACR14= %#x, byVGACR17= %#x, wOffset= %#x\n",
		  wOffsetUpper, byVGACR13, byVGACR14, byVGACR17, wOffset);

	if (byVGACR14 & 0x40)
		dwPitch = wOffset << 3; //DW mode
	else if (byVGACR17 & 0x40)
		dwPitch = wOffset << 1; //byte mode
	else
		dwPitch = wOffset << 2; //word mode

	if (pvg->gmt != TextMode) {
		u8 byBppPowerOfTwo = 0;

		if (pvg->byBitsPerPixel == 32)
			byBppPowerOfTwo = 2;
		else if (pvg->byBitsPerPixel == 16)
			byBppPowerOfTwo = 1;
		else if (pvg->byBitsPerPixel == 8)
			byBppPowerOfTwo = 0;
		else
			byBppPowerOfTwo = 3;	// 4bpp

		//convert it to logic width in pixel
		if (pvg->byBitsPerPixel > 4)
			dwPitch >>= byBppPowerOfTwo;
		else
			dwPitch <<= byBppPowerOfTwo;
	}

	return dwPitch;
}

void update_video_geometry(struct AstRVAS *ast_rvas)
{
	struct ModeInfo *pModeInfo;
	struct NewModeInfoHeader *pNMIH;
	struct DisplayEnd *pDE;
	u8 byNewColorMode = 0;
	u32 VGA_Scratch_Register_350 = 0; //VIDEO_NEW_MODE_INFO_HEADER
	u32 VGA_Scratch_Register_354 = 0; //VIDEO_HDE
	u32 VGA_Scratch_Register_34C = 0; //VIDEO_HDE
	struct VideoGeometry *cur_vg = &ast_rvas->current_vg;

	VGA_Scratch_Register_350 = ast_video_read(ast_rvas->grce_reg_base,
						  AST_VIDEO_SCRATCH_350);
	VGA_Scratch_Register_34C = ast_video_read(ast_rvas->grce_reg_base,
						  AST_VIDEO_SCRATCH_34C);
	VGA_Scratch_Register_354 = ast_video_read(ast_rvas->grce_reg_base,
						  AST_VIDEO_SCRATCH_354);

	pModeInfo = (struct ModeInfo *)(&VGA_Scratch_Register_34C);
	pNMIH = (struct NewModeInfoHeader *)(&VGA_Scratch_Register_350);
	pDE = (struct DisplayEnd *)(&VGA_Scratch_Register_354);
	VIDEO_DBG("pModeInfo: byColorMode: %#x byModeID: %#x byRefreshRateIndex: %#x byScanLines: %#x\n",
		  pModeInfo->byColorMode, pModeInfo->byModeID,
		  pModeInfo->byRefreshRateIndex, pModeInfo->byScanLines);
	VIDEO_DBG("pNMIH: byColorDepth: %#x byDisplayInfo: %#x byMhzPixelClock: %#x byReserved: %#x\n",
		  pNMIH->byColorDepth, pNMIH->byDisplayInfo,
		  pNMIH->byMhzPixelClock, pNMIH->byReserved);
	VIDEO_DBG("pDE: HDE: %#x VDE: %#x\n", pDE->HDE, pDE->VDE);

	byNewColorMode = ((pModeInfo->byColorMode) & 0xf0) >> 4;
	VIDEO_DBG("byNewColorMode= %#x,byModeID=0x%x\n", byNewColorMode,
		  pModeInfo->byModeID);
	ast_video_get_color_mode(byNewColorMode, cur_vg);

	if (pNMIH->byDisplayInfo == MODE_GET_INFO_DE) {
		cur_vg->wScreenWidth = pDE->HDE;
		cur_vg->wScreenHeight = pDE->VDE;
		cur_vg->byBitsPerPixel = pNMIH->byColorDepth;
		cur_vg->byModeID = pModeInfo->byModeID;
	} else {
		ast_video_get_indexed_mode(pModeInfo, cur_vg);
	}

	cur_vg->wStride = (u16)ast_video_get_pitch(ast_rvas);
	VIDEO_DBG("Calculated pitch in pixels= %u\n", cur_vg->wStride);

	if (cur_vg->wStride < cur_vg->wScreenWidth)
		cur_vg->wStride = cur_vg->wScreenWidth;

	VIDEO_DBG("Before current display width %u, height %u, pitch %u, color depth %u, mode %d\n",
		  cur_vg->wScreenWidth, cur_vg->wScreenHeight,
		  cur_vg->wStride, cur_vg->byBitsPerPixel, cur_vg->gmt);

	if (cur_vg->gmt == TextMode ||
	    (cur_vg->gmt == VGAGraphicsMode && pModeInfo->byModeID == 0x13)) {
		ast_video_set_special_modes(pModeInfo, ast_rvas);
	}

	//mode transition
	if (cur_vg->wScreenHeight < 200 || cur_vg->wScreenWidth < 320)
		cur_vg->gmt = InvalidMode;

	if (cur_vg->gmt == TextMode) {
		u8 byVGACR9 = readb((void *)(ast_rvas->grce_reg_base + GRCE_CRTC_OFFSET + 0x9));
		u32 dwCharacterHeight = ((byVGACR9) & 0x1f) + 1;

		VIDEO_DBG("byModeID=0x%x,dwCharacterHeight=%d\n",
			  cur_vg->byModeID, dwCharacterHeight);

		if (dwCharacterHeight != 8 && dwCharacterHeight != 14 &&
		    dwCharacterHeight != 16)
			cur_vg->gmt = InvalidMode;

		if (cur_vg->wScreenWidth > 720 || cur_vg->wScreenHeight > 400)
			cur_vg->gmt = InvalidMode;
	}

	VIDEO_DBG("current display width %u, height %u, pitch %u, color depth %u, mode %d\n",
		  cur_vg->wScreenWidth, cur_vg->wScreenHeight,
		  cur_vg->wStride, cur_vg->byBitsPerPixel, cur_vg->gmt);
}

//check and update current video geometry
bool video_geometry_change(struct AstRVAS *ast_rvas, u32 dwGRCEStatus)
{
	bool b_geometry_changed = false;
	struct VideoGeometry *cur_vg = &ast_rvas->current_vg;
	struct VideoGeometry pre_vg;

	memcpy(&pre_vg, cur_vg, sizeof(pre_vg));
	update_video_geometry(ast_rvas);
	b_geometry_changed = memcmp(&pre_vg, cur_vg, sizeof(struct VideoGeometry))
			!= 0;
	VIDEO_DBG("b_geometry_changed: %d\n", b_geometry_changed);
	return b_geometry_changed;
}

void ioctl_get_video_geometry(struct RvasIoctl *ri, struct AstRVAS *ast_rvas)
{
	memcpy(&ri->vg, &ast_rvas->current_vg, sizeof(struct VideoGeometry));
//	VIDEO_DBG("b_geometry_changed: %d\n", b_geometry_changed);
}

void print_frame_buffer(u32 dwSizeByBytes, struct VGAMemInfo FBInfo)
{
	u32 iter = 0;
	u32 *pdwFrameBufferAddrBase = NULL;
	u32 dwNumMappedPages = 0;

	dwNumMappedPages = ((dwSizeByBytes + 4095) >> 12);
	pdwFrameBufferAddrBase = (u32 *)ioremap(FBInfo.dwFBPhysStart, dwNumMappedPages << 12);

	if (pdwFrameBufferAddrBase) {
		VIDEO_DBG("==============%s===========\n", __func__);

		for (iter = 0; iter < (dwSizeByBytes >> 2); iter++) {
			VIDEO_DBG("0x%x, ", pdwFrameBufferAddrBase[iter]);

			if ((iter % 16) == 0)
				VIDEO_DBG("\n");
		}

		VIDEO_DBG("===========END=============\n");
		iounmap((void *)pdwFrameBufferAddrBase);
	}
}

void ioctl_get_grc_register(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	u32 virt_add = 0;
	u32 size = 0;

	VIDEO_DBG("Start\n");
	virt_add = (u32)get_virt_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	size = ri->rmh1_mem_size;

	if (virt_is_valid_rsvd_mem((u32)ri->rmh, size, pAstRVAS)) {
		memcpy((void *)virt_add,
		       (const void *)(pAstRVAS->grce_reg_base), 0x40);
		memset((void *)(((u8 *)virt_add) + 0x40), 0x0, 0x20);
		memcpy((void *)(((u8 *)virt_add) + 0x60),
		       (const void *)(pAstRVAS->grce_reg_base + 0x60),
		       GRCE_SIZE - 0x60);
		ri->rs = SuccessStatus;
	} else {
		ri->rs = InvalidMemoryHandle;
	}
}

void ioctl_read_snoop_map(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	struct ContextTable *pct = get_context_entry(ri->rc, pAstRVAS);
	u32 virt_add = 0;
	u32 size = 0;

	virt_add = (u32)get_virt_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	size = ri->rmh_mem_size;

	disable_grce_tse_interrupt(pAstRVAS);
	VIDEO_DBG("Start\n");

	if (pct) {
		if (virt_is_valid_rsvd_mem((u32)ri->rmh, size, pAstRVAS)) {
			update_all_snoop_context(pAstRVAS);
			memcpy((void *)virt_add, pct->aqwSnoopMap,
			       sizeof(pct->aqwSnoopMap));

			if (ri->flag) {
				///get the context snoop address
				memset(pct->aqwSnoopMap, 0x00,
				       sizeof(pct->aqwSnoopMap));
				memset(&pct->sa, 0x00, sizeof(pct->sa));
			}
			ri->rs = SuccessStatus;
		} else {
			ri->rs = InvalidMemoryHandle;
		}
	} else {
		ri->rs = InvalidContextHandle;
	}

	enable_grce_tse_interrupt(pAstRVAS);
}

void ioctl_read_snoop_aggregate(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	struct ContextTable *pct = get_context_entry(ri->rc, pAstRVAS);

	disable_grce_tse_interrupt(pAstRVAS);

	if (pct) {
		update_all_snoop_context(pAstRVAS);
		memcpy(&ri->sa, &pct->sa, sizeof(pct->sa));
		VIDEO_DBG("ri->sa.qwCol: %#llx qwRow: %#llx flag: %u\n",
			  ri->sa.qwCol, ri->sa.qwRow, ri->flag);

		if (ri->flag)
			memset(&pct->sa, 0x00, sizeof(pct->sa));

		ri->rs = SuccessStatus;
	} else {
		ri->rs = InvalidContextHandle;
		VIDEO_DBG("Invalid Context\n");
	}

	enable_grce_tse_interrupt(pAstRVAS);
}

void ioctl_set_tse_tsicr(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	u32 addrTSICR;

	pAstRVAS->tse_tsicr = ri->tse_counter;
	addrTSICR = pAstRVAS->fg_reg_base + TSE_TileSnoop_Interrupt_Count;
	writel(pAstRVAS->tse_tsicr, (void *)addrTSICR);// max wait time before interrupt
	ri->rs = SuccessStatus;
}

void ioctl_get_tse_tsicr(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	ri->tse_counter = pAstRVAS->tse_tsicr;
	ri->rs = SuccessStatus;
}

// Get the screen offset from the GRC registers
u32 get_screen_offset(struct AstRVAS *pAstRVAS)
{
	u32 dwScreenOffset = 0;
	u32 addrVGACRC = pAstRVAS->grce_reg_base + GRCE_CRTC + 0xC; // Ch
	u32 addrVGACRD = pAstRVAS->grce_reg_base + GRCE_CRTC + 0xD; // Dh
	u32 addrVGACRAF = pAstRVAS->grce_reg_base + GRCE_CRTCEXT + 0x2F;

	if (pAstRVAS->current_vg.gmt == AGAGraphicsMode) {
		dwScreenOffset = ((readb((void *)addrVGACRAF)) << 16) | ((readb((void *)addrVGACRC)) << 8) |
				  (readb((void *)addrVGACRD));
		dwScreenOffset *= pAstRVAS->current_vg.byBitsPerPixel >> 3;
	}

	VIDEO_DBG("ScreenOffset: %#8.8x\n", dwScreenOffset);

	return dwScreenOffset;
}

void reset_snoop_engine(struct AstRVAS *pAstRVAS)
{
	u32 addr_snoop = pAstRVAS->fg_reg_base + TSE_SnoopMap_Offset;
	u32 reg_value = 0;
	u32 iter;

	writel(0x0, (void *)(pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset));
	writel(0x3, (void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_CS0Reg));
	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_CS1Reg));
	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_RS0Reg));
	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_RS1Reg));

	//Clear TSRR00 to TSRR126 (TSRR01 to TSRR127), Snoop Map
	for (iter = 0; iter < 0x80; ++iter) {
		reg_value = readl((void *)addr_snoop) + 1;
		writel(reg_value, (void *)addr_snoop);
	}

	reg_value = readl((void *)(pAstRVAS->fg_reg_base + TSE_TileCount_Register_Offset));
}

void set_snoop_engine(bool b_geom_chg, struct AstRVAS *pAstRVAS)
{
	u32 tscmd_reg = pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset;
	u32 tsfbsa_reg = pAstRVAS->fg_reg_base + TSE_FrameBuffer_Offset;
	u32 tsulr_reg = pAstRVAS->fg_reg_base + TSE_UpperLimit_Offset;
	u32 new_tsfbsa = 0;
	u32 tscmd = 0;
	u8 byBytesPerPixel = 0x0;
	u8 byTSCMDBytesPerPixel = 0x0;
	int cContext;
	u32 dwStride;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	// Calculate Start Address into the Frame Buffer
	new_tsfbsa = get_screen_offset(pAstRVAS);
	tscmd = readl((void *)(pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset));

	tscmd &= (1 << TSCMD_INT_ENBL_BIT);

	VIDEO_DBG("Latest TSFBSA: %#8.8x\n", new_tsfbsa);
	VIDEO_DBG("pAstRVAS->current_vg: bpp %u Mode:%#x gmt:%d Width:%u Height:%u Stride:%u\n",
		  pAstRVAS->current_vg.byBitsPerPixel,
		  pAstRVAS->current_vg.byModeID, pAstRVAS->current_vg.gmt,
		  pAstRVAS->current_vg.wScreenWidth,
		  pAstRVAS->current_vg.wScreenHeight,
		  pAstRVAS->current_vg.wStride);

	if (b_geom_chg || (readl((void *)tsfbsa_reg) != new_tsfbsa)) {
		byBytesPerPixel = pAstRVAS->current_vg.byBitsPerPixel >> 3;

		if (pAstRVAS->current_vg.gmt == VGAGraphicsMode ||
		    pAstRVAS->current_vg.byBitsPerPixel == 4) {
			byTSCMDBytesPerPixel = 0;
		} else {
			switch (byBytesPerPixel) {
			case 1:
				byTSCMDBytesPerPixel = 0;
				break;

			case 2:
				byTSCMDBytesPerPixel = 1;
				break;

			case 3:
			case 4:
				byTSCMDBytesPerPixel = 2;
				break;
			}
		}
		dwStride = pAstRVAS->current_vg.wStride;

		if (byBytesPerPixel == 3)
			dwStride = (dwStride + dwStride + dwStride) >> 2;
		else if (pAstRVAS->current_vg.byBitsPerPixel == 4)
			dwStride >>= 1;

		// set TSE SCR
		// start the tile snoop engine
		// flip the 15 bit
		if (!(readl((void *)tscmd_reg) & TSCMD_SCREEN_OWNER))
			tscmd |= TSCMD_SCREEN_OWNER;

		tscmd |= (dwStride << TSCMD_PITCH_BIT) | (1 << TSCMD_CPT_BIT)
			| (1 << TSCMD_RPT_BIT)
			| (byTSCMDBytesPerPixel << TSCMD_BPP_BIT)
			| (1 << TSCMD_VGA_MODE_BIT) | (1 << TSCMD_TSE_ENBL_BIT);
		VIDEO_DBG("tscmd: %#8.8x\n", tscmd);
		// set the TSFBSA & TSULR
		writel(new_tsfbsa, (void *)tsfbsa_reg);
		writel(BSE_UPPER_LIMIT, (void *)tsulr_reg);
		writel(tscmd, (void *)tscmd_reg);
		//reset snoop information
		get_snoop_map_data(pAstRVAS);
		memset((void *)pAstRVAS->accrued_sm, 0,
		       sizeof(pAstRVAS->accrued_sm));
		memset((void *)&pAstRVAS->accrued_sa, 0,
		       sizeof(pAstRVAS->accrued_sa));

		for (cContext = 0; cContext < MAX_NUM_CONTEXT; cContext++) {
			if (ppctContextTable[cContext]) {
				memset(ppctContextTable[cContext]->aqwSnoopMap,
				       0,
				       sizeof(ppctContextTable[cContext]->aqwSnoopMap));
				memset(&ppctContextTable[cContext]->sa, 0,
				       sizeof(ppctContextTable[cContext]->sa));
			}
		}      // for each context
	} // if
}

//
// ReadSnoopMap to Clear
//
void get_snoop_map_data(struct AstRVAS *pAstRVAS)
{
	u32 dwSMDword;
	u64 aqwSnoopMap[SNOOP_MAP_QWORD_COUNT];
	//u32 dw_iter;

	get_snoop_aggregate(pAstRVAS);
	memcpy((void *)aqwSnoopMap,
	       (const void *)(pAstRVAS->fg_reg_base + TSE_SnoopMap_Offset),
	       sizeof(aqwSnoopMap));

	//VIDEO_DBG("Snoop Map:\n");
	//VIDEO_DBG("==========\n");

	//for (dw_iter = 0; dw_iter < SNOOP_MAP_QWORD_COUNT; ++dw_iter)
		//VIDEO_DBG("[%2u]: 0x%16.16llx\n", dw_iter, aqwSnoopMap[dw_iter]);

	//VIDEO_DBG("==========\n\n");

	// copy 512 snoop map
	for (dwSMDword = 0; dwSMDword < SNOOP_MAP_QWORD_COUNT; ++dwSMDword)
		pAstRVAS->accrued_sm[dwSMDword] |= aqwSnoopMap[dwSMDword];
}

void get_snoop_aggregate(struct AstRVAS *pAstRVAS)
{
	u64 qwRow = 0;
	u64 qwCol = 0;

	// copy the snoop aggregate,row 64 bits
	qwRow = readl((void *)(pAstRVAS->fg_reg_base + TSE_RS1Reg));
	qwRow = qwRow << 32;
	qwRow |= readl((void *)(pAstRVAS->fg_reg_base + TSE_RS0Reg));

	// column
	qwCol = readl((void *)(pAstRVAS->fg_reg_base + TSE_CS1Reg));
	qwCol = qwCol << 32;
	qwCol |= readl((void *)(pAstRVAS->fg_reg_base + TSE_CS0Reg));

	VIDEO_DBG("Snoop Aggregate Row: 0x%16.16llx\n", qwRow);
	VIDEO_DBG("Snoop Aggregate Col: 0x%16.16llx\n", qwCol);
	VIDEO_DBG("DRIVER:: %s\n", __func__);
	VIDEO_DBG("DRIVER:: row [%#llx]\n", qwRow);
	VIDEO_DBG("DRIVER:: col [%#llx]\n", qwCol);

	pAstRVAS->accrued_sa.qwCol |= qwCol;
	pAstRVAS->accrued_sa.qwRow |= qwRow;
}

u64 reinterpret_32bpp_snoop_row_as_24bpp(u64 theSnoopRow)
{
	u64 qwResult = 0;
	u64 qwSourceBit = 1;
	u32 cSourceBit;
	u64 qwBitResult = 0;

	for (cSourceBit = 0; cSourceBit < 64; ++cSourceBit) {
		if (theSnoopRow & qwSourceBit) {
			qwBitResult = ((cSourceBit * 128) / 96);
			qwResult |= (((u64)3) << qwBitResult);
		}

		qwSourceBit <<= 1;
	}

	return qwResult;
}

//
//one tile: 32x32,
//
void convert_snoop_map(struct AstRVAS *pAstRVAS)
{
	u32 dwAllRows = (pAstRVAS->current_vg.wScreenHeight + 31) >> 5;
	u32 cRow;

	for (cRow = 0; cRow < dwAllRows; ++cRow)
		pAstRVAS->accrued_sm[cRow] =
		reinterpret_32bpp_snoop_row_as_24bpp(pAstRVAS->accrued_sm[cRow]);

	pAstRVAS->accrued_sa.qwCol =
	reinterpret_32bpp_snoop_row_as_24bpp(pAstRVAS->accrued_sa.qwCol);
}

void update_all_snoop_context(struct AstRVAS *pAstRVAS)
{
	u32 cContext;
	u32 iSMDword;
	struct ContextTable **ppctContextTable = pAstRVAS->ppctContextTable;

	if (pAstRVAS->current_vg.byBitsPerPixel == 24)
		convert_snoop_map(pAstRVAS);

	for (cContext = 0; cContext < MAX_NUM_CONTEXT; cContext++)
		if (ppctContextTable[cContext]) {
			for (iSMDword = 0; iSMDword < SNOOP_MAP_QWORD_COUNT;
				iSMDword++)
				ppctContextTable[cContext]->aqwSnoopMap[iSMDword] |=
					pAstRVAS->accrued_sm[iSMDword];

			ppctContextTable[cContext]->sa.qwRow |=
				pAstRVAS->accrued_sa.qwRow;
			ppctContextTable[cContext]->sa.qwCol |=
				pAstRVAS->accrued_sa.qwCol;
		}

	//reset snoop map and aggregate
	memset((void *)pAstRVAS->accrued_sm, 0, sizeof(pAstRVAS->accrued_sm));
	memset((void *)&pAstRVAS->accrued_sa, 0x00,
	       sizeof(pAstRVAS->accrued_sa));
}

static u32 setup_tfe_cr(struct FetchOperation *pfo)
{
	u32 dwTFECR = 0;

	if (pfo->bEnableRLE)
		dwTFECR = (pfo->byRLETripletCode << 24)
			| (pfo->byRLERepeatCode << 16);

	dwTFECR &= TFCTL_DESCRIPTOR_IN_DDR_MASK;
	dwTFECR |= 1;
	dwTFECR |= 1 << 1; // enabled IRQ
	VIDEO_DBG("dwTFECR: %#x\n", dwTFECR);
	return dwTFECR;
}

static void start_skip_mode_skip(struct Descriptor *pDescriptorVirtualAddr,
				 u32 dwDescPhysicalAddr,
				 u32 dwSourceAddr, u32 dwDestAddr, u16 wStride,
				 u8 bytesPerPixel, u32 dwFetchWidthPixels,
				 u32 dwFetchHeight, bool bRLEOverFLow)
{
	struct Descriptor *pVirtDesc = pDescriptorVirtualAddr;

	// Fetch Skipping data to a temp buffer
	prepare_tfe_descriptor(pVirtDesc, dwSourceAddr, dwDestAddr, true, 1,
			       false, wStride, bytesPerPixel,
			       dwFetchWidthPixels, dwFetchHeight,
			       LowByteMode, bRLEOverFLow, 0);

	dwDestAddr += dwFetchWidthPixels * dwFetchHeight;
	pVirtDesc++;

	if (bytesPerPixel == 3 || bytesPerPixel == 4) {
		prepare_tfe_descriptor(pVirtDesc, dwSourceAddr, dwDestAddr,
				       true, 1, false, wStride, bytesPerPixel,
				       dwFetchWidthPixels, dwFetchHeight,
				       MiddleByteMode, bRLEOverFLow, 0);

		dwDestAddr += dwFetchWidthPixels * dwFetchHeight;
		pVirtDesc++;
	}

	prepare_tfe_descriptor(pVirtDesc, dwSourceAddr, dwDestAddr, false, 1,
			       false, wStride, bytesPerPixel,
			       dwFetchWidthPixels, dwFetchHeight,
			       TopByteMode, bRLEOverFLow, 1);
}

// calculate pure fetch size
static u32 calculate_fetch_size(enum SelectedByteMode sbm, u8 bytesPerPixel,
				u32 dwFetchWidthPixels, u32 dwFetchHeight)
{
	u32 dwFetchSize = 0;

	switch (sbm) {
	case AllBytesMode:
		dwFetchSize = dwFetchWidthPixels * dwFetchHeight
			* bytesPerPixel;
		break;

	case SkipMode:
		if (bytesPerPixel == 3 || bytesPerPixel == 4)
			dwFetchSize = dwFetchWidthPixels * dwFetchHeight * 3;
		else
			dwFetchSize = dwFetchWidthPixels * dwFetchHeight
				* bytesPerPixel;
		break;

	case PlanarToPackedMode:
		dwFetchSize = (dwFetchWidthPixels * dwFetchHeight);
		break;

	case PackedToPackedMode:
		break;

	default:
		VIDEO_DBG("Mode= %d is not supported\n", sbm);
		break;
	} //switch
	return dwFetchSize;
}

static void display_fetch_info(struct FetchVideoTilesArg *pFVTDescriptor, u32 dwCD)
{
	struct FetchRegion *pfr = NULL;

	pfr = &pFVTDescriptor->pfo[dwCD].fr;
	VIDEO_DBG("FETCH - 1 dwCD: %u\n", dwCD);
	VIDEO_DBG("pfr->wLeftX :%d\n", pfr->wLeftX);
	VIDEO_DBG("pfr->wTopY :%d\n", pfr->wTopY);
	VIDEO_DBG("pfr->wRightX :%d\n", pfr->wRightX);
	VIDEO_DBG("pfr->wBottomY :%d\n", pfr->wBottomY);
	VIDEO_DBG(" bEanbleRLE %d\n", pFVTDescriptor->pfo[dwCD].bEnableRLE);
	VIDEO_DBG("Stride : %d\n", pFVTDescriptor->vg.wStride);
}

void ioctl_fetch_video_tiles(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	struct FetchVideoTilesArg *pFVTDescriptor;
	u32 dwCD = 0;
	struct Descriptor *pDescriptorVirtualAddr;
	u32 dwDescPhysicalAddr;
	u32 dwSourcePhyAddr;
	u32 dwDestinationPhyAddr;
	u8 bytesPerPixel;
	struct FetchRegion *pfr;
	bool bNotLastEntry = false;
	u32 dwTFECR = 0;
	u32 dwTotalFetchSize = 0;
	u32 dwRLESize = 0;
	bool bRLEOverFLow = false;
	u32 dwFetchWidthPixels = 0;
	u32 dwFetchHeight = 0;
	u32 arg_phys = 0;
	u32 data_phys_out = 0;
	u32 data_phys_temp = 0;
	u16 stride = 0;
	bool bSkippingMode = false;
	void *desc_virt = NULL;
	u32 desc_phy = 0;
	struct ContextTable *ctx_entry = NULL;

	VIDEO_DBG("DRIVER:::: TILE FETCH CHAINING\n");
	ctx_entry = get_context_entry(ri->rc, pAstRVAS);

	if (ctx_entry) {
		desc_virt = ctx_entry->desc_virt;
		desc_phy = ctx_entry->desc_phy;
	} else {
		VIDEO_DBG("Returning with invalid Context handle: 0x%p\n", ri->rc);
		ri->rs = InvalidContextHandle;
		return;
	}

	ri->rs = SuccessStatus;
	//struct FetchVideoTilesArg buffer
	arg_phys = get_phys_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	//Fetch final dest buffer
	data_phys_out = get_phys_add_rsvd_mem((u32)ri->rmh1, pAstRVAS);
	//Intermediate Buffer
	data_phys_temp = get_phys_add_rsvd_mem((u32)ri->rmh2, pAstRVAS);

	dwDestinationPhyAddr = data_phys_out;
	pFVTDescriptor = (struct FetchVideoTilesArg *)get_virt_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	VIDEO_DBG("Destination virtual Add: 0x%p\n", get_virt_add_rsvd_mem((u32)ri->rmh, pAstRVAS));
	VIDEO_DBG("Destination Physical Add: %#x\n", dwDestinationPhyAddr);
	memset(desc_virt, 0x00, PAGE_SIZE);

	if (arg_phys && data_phys_out && data_phys_temp) {
		pDescriptorVirtualAddr = (struct Descriptor *)desc_virt;
		dwDescPhysicalAddr = desc_phy;
		VIDEO_DBG("Descriptor Virtual Addr: %#x\n",
			  (u32)pDescriptorVirtualAddr);
		VIDEO_DBG("Descriptor Physical Addr: %#x\n", dwDescPhysicalAddr);
		stride = pFVTDescriptor->vg.wStride;

		if (pFVTDescriptor->vg.byBitsPerPixel == 4) {
			bytesPerPixel = 1;
			stride >>= 1;
		} else {
			bytesPerPixel = pFVTDescriptor->vg.byBitsPerPixel >> 3;
		}

		VIDEO_DBG("u8 per pixel:%u\n", bytesPerPixel);
		// fetch all data to Destination 1 without RLE
		VIDEO_DBG("FETCH - 0\n");
		VIDEO_DBG("COUNT OF Operation: %u\n", pFVTDescriptor->cfo);

		for (dwCD = 0; dwCD < pFVTDescriptor->cfo; dwCD++) {
			display_fetch_info(pFVTDescriptor, dwCD);
			// Set up Control Register.
			dwTFECR = setup_tfe_cr(&pFVTDescriptor->pfo[dwCD]);
			pfr = &pFVTDescriptor->pfo[dwCD].fr;
			// find Source Address
			if (pFVTDescriptor->vg.byBitsPerPixel == 4) {
				dwSourcePhyAddr = get_phy_fb_start_address(pAstRVAS)
								+ ((pfr->wLeftX * bytesPerPixel) >> 1)
								+ pfr->wTopY * stride
								* bytesPerPixel;

				dwFetchWidthPixels = (pfr->wRightX - pfr->wLeftX + 1) >> 1;
			} else {
				dwSourcePhyAddr = get_phy_fb_start_address(pAstRVAS)
						+ pfr->wLeftX * bytesPerPixel
						+ pfr->wTopY * stride
						* bytesPerPixel;

				dwFetchWidthPixels = (pfr->wRightX - pfr->wLeftX + 1);
			}
			VIDEO_DBG("dwCD: %u dwSourcePhyAddr: %#x\n", dwCD,
				  dwSourcePhyAddr);
			dwFetchHeight = pfr->wBottomY - pfr->wTopY + 1;

			VIDEO_DBG("DESCRIPTOR virtual ADDRESS: 0x%p\n",
				  pDescriptorVirtualAddr);
			if (pFVTDescriptor->vg.byBitsPerPixel == 4)
				pFVTDescriptor->pfo[dwCD].sbm =
					PlanarToPackedMode;

			pFVTDescriptor->pfo[dwCD].dwFetchSize =
				calculate_fetch_size(pFVTDescriptor->pfo[dwCD].sbm,
						     bytesPerPixel, dwFetchWidthPixels,
						     dwFetchHeight);
			bSkippingMode =
				(pFVTDescriptor->pfo[dwCD].sbm == SkipMode) ?
				true : false;

			if (bSkippingMode && bytesPerPixel > 1) {
				u32 skipSrcAddr = dwSourcePhyAddr;
				u32 skipDestAddr = dwDestinationPhyAddr;
				u8 byPostBytesPerPixel =
					(bytesPerPixel == 2) ? 2 : 3;
				VIDEO_DBG("In SkippingMode...\n");

				if (pFVTDescriptor->pfo[dwCD].bEnableRLE) {
					//skip data to intermediate buffer
					skipDestAddr = data_phys_temp;
				}

				start_skip_mode_skip(pDescriptorVirtualAddr,
						     dwDescPhysicalAddr, skipSrcAddr,
						     skipDestAddr,
						     pFVTDescriptor->vg.wStride,
						     bytesPerPixel, dwFetchWidthPixels,
						     dwFetchHeight, bRLEOverFLow);

				if (pFVTDescriptor->pfo[dwCD].bEnableRLE) {
					u32 rleSrcAddr = skipDestAddr;
					u32 rleDesAddr = dwDestinationPhyAddr;

					///// take second look at skip mode for using map single
					if (sleep_on_tfe_busy(pAstRVAS,
							      dwDescPhysicalAddr, // Descriptor physical Address
							      dwTFECR, // control register value
							      pFVTDescriptor->pfo[dwCD].dwFetchSize, // bandwidth limitor value
							      &dwRLESize,    // out:: rle size
							      &pFVTDescriptor->pfo[dwCD].dwCheckSum
							      ) == false) { // out:: cs size
						ri->rs = GenericError;
						return;
					}

					// perform RLE from Temp buffer to dwDestinationPhyAddr
					//VIDEO_DBG("skip rle\n");
					prepare_tfe_descriptor(pDescriptorVirtualAddr,
							       rleSrcAddr, rleDesAddr,
							       bNotLastEntry, 1,
							       pFVTDescriptor->pfo[dwCD].bEnableRLE,
							       dwFetchWidthPixels,
							       byPostBytesPerPixel,
							       dwFetchWidthPixels,
							       dwFetchHeight, AllBytesMode,
							       bRLEOverFLow, 1);
				}
			} else {
				VIDEO_DBG("Prep TFE Desc - no skipping\n");
				prepare_tfe_descriptor(pDescriptorVirtualAddr,
						       dwSourcePhyAddr, dwDestinationPhyAddr,
						       bNotLastEntry, 1,
						       pFVTDescriptor->pfo[dwCD].bEnableRLE,
						       stride, bytesPerPixel,
						       dwFetchWidthPixels, dwFetchHeight,
						       pFVTDescriptor->pfo[dwCD].sbm,
						       bRLEOverFLow, 1);
				VIDEO_DBG("Done prep TFE Desc-no skipping\n");
			}
			VIDEO_DBG("Sleeping while TFE is busy...\n");

			if (sleep_on_tfe_busy(pAstRVAS, dwDescPhysicalAddr, // Descriptor physical Address
					      dwTFECR,               // control register value
					      pFVTDescriptor->pfo[dwCD].dwFetchSize, // bandwidth limitor value
					      &dwRLESize,                    // out:: rle size
					      &pFVTDescriptor->pfo[dwCD].dwCheckSum
					      ) == false) {  // out:: cs size
				ri->rs = GenericError;
				return;
			}

			VIDEO_DBG("After sleep where TFE was busy\n");

			//VIDEO_DBG("skip rle end\n");
			if (!pFVTDescriptor->pfo[dwCD].bEnableRLE) { // RLE not enabled
				VIDEO_DBG("RLE is off\n");
				pFVTDescriptor->pfo[dwCD].bRLEFailed = false;
				dwRLESize =
					pFVTDescriptor->pfo[dwCD].dwFetchSize;
				dwTotalFetchSize +=
					pFVTDescriptor->pfo[dwCD].dwFetchSize;
			} else { // RLE enabled
				VIDEO_DBG("RLE Enabled\n");
				if (dwRLESize
					>= pFVTDescriptor->pfo[dwCD].dwFetchSize) { // FAILED
					VIDEO_DBG("DRVIER:: RLE failed RLE: %u > %u\n",
						  dwRLESize,
						  pFVTDescriptor->pfo[dwCD].dwFetchSize);
					pFVTDescriptor->pfo[dwCD].bRLEFailed =
						true;

					if (bSkippingMode) {
						u32 skipSrcAddr =
							dwSourcePhyAddr;
						u32 skipDestAddr =
							dwDestinationPhyAddr;

						start_skip_mode_skip(pDescriptorVirtualAddr,
								     dwDescPhysicalAddr,
								     skipSrcAddr,
								     skipDestAddr,
								     pFVTDescriptor->vg.wStride,
								     bytesPerPixel,
								     dwFetchWidthPixels,
								     dwFetchHeight,
								     bRLEOverFLow);
					} else {
						VIDEO_DBG(" FETCH - 4\n");
						prepare_tfe_descriptor(pDescriptorVirtualAddr,
								       dwSourcePhyAddr,
								       dwDestinationPhyAddr,
								       bNotLastEntry, 1, false,
								       pFVTDescriptor->vg.wStride,
								       bytesPerPixel,
								       dwFetchWidthPixels,
								       dwFetchHeight,
								       pFVTDescriptor->pfo[dwCD].sbm,
								       bRLEOverFLow, 1);
					}

					if (sleep_on_tfe_busy(pAstRVAS,
							      dwDescPhysicalAddr, // Descriptor physical Address
							      dwTFECR, // control register value
							      pFVTDescriptor->pfo[dwCD].dwFetchSize, // bandwidth limitor value
							      &dwRLESize,    // out:: rle size
							      &pFVTDescriptor->pfo[dwCD].dwCheckSum
							      ) == false) {  // out:: cs size
						ri->rs = GenericError;
						return;
					}

					dwTotalFetchSize +=
						pFVTDescriptor->pfo[dwCD].dwFetchSize;
					dwRLESize =
						pFVTDescriptor->pfo[dwCD].dwFetchSize;
				}  else { //RLE successful
					pFVTDescriptor->pfo[dwCD].bRLEFailed =
						false;
					dwTotalFetchSize += dwRLESize;
					dwTotalFetchSize = (dwTotalFetchSize
						+ 0x3) & 0xfffffffc;
				}
			} //RLE Enabled

			pFVTDescriptor->pfo[dwCD].dwFetchRLESize = dwRLESize;
			VIDEO_DBG("DRIVER:: RLE: %u, nonRLE: %u\n", dwRLESize,
				  pFVTDescriptor->pfo[dwCD].dwFetchSize);
			VIDEO_DBG("FETCH:: loop FETCH size: %u\n", dwTotalFetchSize);
			dwDestinationPhyAddr = data_phys_out + dwTotalFetchSize;
		} //for TFE

		pFVTDescriptor->dwTotalOutputSize = dwTotalFetchSize;
		VIDEO_DBG("Fetch Size: %#x\n", dwTotalFetchSize);
	} else {
		dev_err(pAstRVAS->pdev, "Memory allocation failure\n");
		ri->rs = InvalidMemoryHandle;
	}
} // End - ioctl_fetch_video_tiles

void prepare_ldma_descriptor(struct Descriptor *pDAddress, u32 dwSourceAddress,
			     u32 dwDestAddress, u32 dwLDMASize, u8 byNotLastEntry)
{
	u8 byInterrupt = 0;

	VIDEO_DBG("pDAddress: 0x%p\n", pDAddress);

	// initialize to 0
	pDAddress->dw0General = 0x00;
	pDAddress->dw1FetchWidthLine = 0x00;
	pDAddress->dw2SourceAddr = 0x00;
	pDAddress->dw3DestinationAddr = 0x00;

	// initialize to 0
	if (!byNotLastEntry)
		byInterrupt = 0x1;

	pDAddress->dw0General = ((dwLDMASize - 1) << 8) | (byNotLastEntry << 1)
		| byInterrupt;
	pDAddress->dw2SourceAddr = dwSourceAddress;
	pDAddress->dw3DestinationAddr = dwDestAddress;

	VIDEO_DBG("u32 0: 0x%x\n", pDAddress->dw0General);
	VIDEO_DBG("u32 1: 0x%x\n", pDAddress->dw1FetchWidthLine);
	VIDEO_DBG("u32 2: 0x%x\n", pDAddress->dw2SourceAddr);
	VIDEO_DBG("u32 3: 0x%x\n", pDAddress->dw3DestinationAddr);
}

//
// ioctl_run_length_encode_data - encode buffer data
//
void ioctl_run_length_encode_data(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	struct Descriptor *pDescriptorAdd = NULL;
	struct Descriptor *pDescriptorAddPhys = NULL;
	u8 bytesPerPixel;
	bool bNotLastEntry = true;
	u32 dwTFECR = 0;
	bool bRLEOverFLow = false;
	u32 dwFetchWidthPixels = 0;
	u32 dwFetchHeight = 0;
	u32 dwPhysAddIn;
	u32 dwPhysAddOut;
	u32 data_size = 0;
	void *desc_virt = NULL;
	u32 desc_phy = 0;
	struct ContextTable *ctx_entry = NULL;

	ctx_entry = get_context_entry(ri->rc, pAstRVAS);
	if (ctx_entry) {
		desc_virt = ctx_entry->desc_virt;
		desc_phy = ctx_entry->desc_phy;
	} else {
		ri->rs = InvalidContextHandle;
		return;
	}

	ri->rs = SuccessStatus;

	dwPhysAddIn = get_phys_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	dwPhysAddOut = get_phys_add_rsvd_mem((u32)ri->rmh1, pAstRVAS);

	data_size = ri->rmh_mem_size;
	pDescriptorAdd = (struct Descriptor *)ctx_entry->desc_virt;
	pDescriptorAddPhys = (struct Descriptor *)ctx_entry->desc_phy;

	VIDEO_DBG("pDescriptorAdd=%#x, phy=%#x\n", (u32)pDescriptorAdd,
		  (u32)pDescriptorAddPhys);

	if (dwPhysAddIn && dwPhysAddOut) {
		// Enable TFE
		dwTFECR = (ri->encode & 0xffff0000) << 16;
		dwTFECR |= 1;
		dwTFECR &= TFCTL_DESCRIPTOR_IN_DDR_MASK;

		// triplet code and repeat code
		bNotLastEntry = false;
		bRLEOverFLow = true;
		dwFetchWidthPixels = TILE_SIZE;
		dwFetchHeight = data_size / TILE_SIZE;
		bytesPerPixel = 1;

		prepare_tfe_descriptor(pDescriptorAdd, dwPhysAddIn,
				       dwPhysAddOut, bNotLastEntry, 1,
				       1, dwFetchWidthPixels,
				       bytesPerPixel, dwFetchWidthPixels,
				       dwFetchHeight, AllBytesMode, bRLEOverFLow, 1);

		if (sleep_on_tfe_busy(pAstRVAS, (u32)pDescriptorAddPhys,
				      dwTFECR, data_size, &ri->rle_len,
				      &ri->rle_checksum) == false) {
			ri->rs = GenericError;
			dev_err(pAstRVAS->pdev, "%s sleep_on_tfe_busy ERROR\n", __func__);
			return;
		}
	} else {
		ri->rs = InvalidMemoryHandle;
	}
}

static u32 get_video_slice_fetch_width(u8 cBuckets)
{
	u32 dwFetchWidthPixels = 0;

	switch (cBuckets) {
	case 3:
		dwFetchWidthPixels = ((TILE_SIZE << 5) * 3) >> 3;
		break;

	case 8:
		dwFetchWidthPixels = TILE_SIZE << 5;
		break;

	case 16:
		dwFetchWidthPixels = (TILE_SIZE << 5) * 2;
		break;

	case 24:
		dwFetchWidthPixels = (TILE_SIZE << 5) * 3;
		break;

	default:
		dwFetchWidthPixels = TILE_SIZE << 2;
		break;
	}

	return dwFetchWidthPixels;
}

void ioctl_fetch_video_slices(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	struct FetchVideoSlicesArg *pFVSA;
	u32 dwCD;
	struct Descriptor *pDescriptorVirtualAddr;
	u32 dwDescPhysicalAddr;
	u32 dwSourceAddress;
	u32 dwDestinationAddress1Index;
	u8 bytesPerPixel;
	bool bNotLastEntry = true;
	bool bInterrupt = false;
	u32 dwTFECR = 0;
	u32 dwFetchSize = 0;
	bool bRLEOverFLow = false;
	u32 dwFetchWidthPixels = 0;
	u32 dwFetchHeight = 0;
	u32 arg_phys = 0;
	u32 data_phys_out = 0;
	u32 data_phys_rle = 0;
	struct BSEAggregateRegister aBSEAR;
	struct Descriptor *pNextDescriptor = 0;
	u32 dwNexDestAddr = 0;
	u32 dwBucketSizeIter = 0;
	bool bBucketSizeEnable = 0;
	u32 addrBSCR = pAstRVAS->fg_reg_base + BSE_Command_Register;
	void *desc_virt = NULL;
	u32 desc_phy = 0;
	struct ContextTable *ctx_entry = get_context_entry(ri->rc, pAstRVAS);

	VIDEO_DBG("Start\n");

	if (ctx_entry) {
		desc_virt = ctx_entry->desc_virt;
		desc_phy = ctx_entry->desc_phy;
	} else {
		pr_err("BSE: Cannot get valid context\n");
		ri->rs = InvalidContextHandle;
		return;
	}

	arg_phys = get_phys_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	data_phys_out = get_phys_add_rsvd_mem((u32)ri->rmh1, pAstRVAS);
	data_phys_rle = get_phys_add_rsvd_mem((u32)ri->rmh2, pAstRVAS);

	if (!arg_phys || !data_phys_out || !data_phys_rle) {
		pr_err("BSE: Invalid memory handle\n");
		ri->rs = InvalidMemoryHandle;
		return;
	}
	ri->rs = SuccessStatus;
	dwDestinationAddress1Index = data_phys_out;
	pFVSA = (struct FetchVideoSlicesArg *)get_virt_add_rsvd_mem((u32)ri->rmh, pAstRVAS);

	VIDEO_DBG("bEnableRLE: %d cBuckets: %u cfr: %u\n", pFVSA->bEnableRLE,
		  pFVSA->cBuckets, pFVSA->cfr);

	if (pFVSA->cfr > 1) {
		writel(readl((void *)addrBSCR) | BSE_ENABLE_MULT_BUCKET_SZS, (void *)addrBSCR);
		bBucketSizeEnable = 1;
	} else {
		writel(readl((void *)addrBSCR) & (~BSE_ENABLE_MULT_BUCKET_SZS), (void *)addrBSCR);
		bBucketSizeEnable = 0;
	}

	VIDEO_DBG("*pdwBSCR: %#x bBucketSizeEnable: %d\n", readl((void *)addrBSCR),
		  bBucketSizeEnable);

	pDescriptorVirtualAddr = ctx_entry->desc_virt;
	dwDescPhysicalAddr = ctx_entry->desc_phy;
	bytesPerPixel = pFVSA->vg.byBitsPerPixel >> 3;

	VIDEO_DBG("BSE:: u8 per pixel: %d\n", bytesPerPixel);
	VIDEO_DBG("BSE:: cfr: %u bucket size: %d\n", pFVSA->cfr, pFVSA->cBuckets);

	pNextDescriptor = pDescriptorVirtualAddr;
	dwNexDestAddr = dwDestinationAddress1Index;
	// Prepare BSE Descriptors for all Regions
	VIDEO_DBG("pNextDescriptor 0x%p dwNexDestAddr: %#x\n", pNextDescriptor,
		  dwNexDestAddr);

	for (dwCD = 0; dwCD < pFVSA->cfr; dwCD++) {
		VIDEO_DBG("dwCD: %u\n", dwCD);
		VIDEO_DBG("pfr->wLeftX :%d\n", pFVSA->pfr[dwCD].wLeftX);
		VIDEO_DBG("pfr->wTopY :%d\n", pFVSA->pfr[dwCD].wTopY);
		VIDEO_DBG("pfr->wRightX :%d\n", pFVSA->pfr[dwCD].wRightX);
		VIDEO_DBG("pfr->wBottomY :%d\n", pFVSA->pfr[dwCD].wBottomY);

		dwSourceAddress = get_phy_fb_start_address(pAstRVAS)
			+ pFVSA->pfr[dwCD].wLeftX * bytesPerPixel
			+ pFVSA->pfr[dwCD].wTopY * pFVSA->vg.wStride
			* bytesPerPixel;
		dwFetchWidthPixels = (pFVSA->pfr[dwCD].wRightX
			- pFVSA->pfr[dwCD].wLeftX + 1);
		dwFetchHeight = pFVSA->pfr[dwCD].wBottomY
			- pFVSA->pfr[dwCD].wTopY + 1;

		VIDEO_DBG("BSE Width in Pixel: %d\n", dwFetchWidthPixels);
		VIDEO_DBG("BSE Height: %d bBucketSizeEnable: %d\n", dwFetchHeight,
			  bBucketSizeEnable);

		if (!bBucketSizeEnable) {
			bNotLastEntry = false;
			bInterrupt = true;
			prepare_bse_descriptor(pDescriptorVirtualAddr,
					       dwSourceAddress, dwDestinationAddress1Index,
					       bNotLastEntry, pFVSA->vg.wStride, bytesPerPixel,
					       dwFetchWidthPixels, dwFetchHeight, bInterrupt);
			dwFetchSize += (pFVSA->cBuckets
				* (dwFetchWidthPixels * dwFetchHeight) >> 3);
			aBSEAR = setUp_bse_bucket(pFVSA->abyBitIndexes,
						  pFVSA->cBuckets, bytesPerPixel,
						  dwFetchWidthPixels, dwFetchHeight);

		} else {
			if (dwCD == pFVSA->cfr - 1) {
				bNotLastEntry = false;
				bInterrupt = true;
			} else {
				bNotLastEntry = true;
				bInterrupt = false;
			}

			prepare_bse_descriptor_2(pNextDescriptor,
						 dwSourceAddress,
						 dwNexDestAddr, bNotLastEntry,
						 pFVSA->vg.wStride, bytesPerPixel,
						 dwFetchWidthPixels, dwFetchHeight,
						 bInterrupt,
						 arrBuckSizeRegIndex[dwBucketSizeIter]);

			aBSEAR = set_up_bse_bucket_2(pAstRVAS,
						     pFVSA->abyBitIndexes, pFVSA->cBuckets,
						     bytesPerPixel, dwFetchWidthPixels,
						     dwFetchHeight,
						     arrBuckSizeRegIndex[dwBucketSizeIter]);

			dwBucketSizeIter++;
			pNextDescriptor++;
			dwFetchSize += pFVSA->cBuckets
				* ((dwFetchWidthPixels * dwFetchHeight) >> 3); //each bucket size
			dwNexDestAddr = dwDestinationAddress1Index
				+ dwFetchSize;
		}
	}

	//bse now
	if (pFVSA->cBuckets <= FULL_BUCKETS_COUNT) {
		if (bBucketSizeEnable)
			aBSEAR.dwBSDBS = 0x80000000;

		VIDEO_DBG("Sleeping on BSE to complete\n");

		if (sleep_on_bse_busy(pAstRVAS, dwDescPhysicalAddr, aBSEAR,
				      dwFetchSize) == false) {
			dev_err(pAstRVAS->pdev, ".....BSE Timeout\n");
			ri->rs = GenericError;
			return;
		}
	}
	VIDEO_DBG("Fetched the bit slices\n");
	//RLE
	pFVSA->dwSlicedSize = dwFetchSize;
	pFVSA->dwSlicedRLESize = pFVSA->dwSlicedSize;

	// do RLE if RLE is on. Fetch from Destination 1 to Destination 2 with RLE on
	bNotLastEntry = false;

	if (pFVSA->bEnableRLE) {
		VIDEO_DBG("BSE - 3 (RLE Enabled)\n");
		// Enable TFE
		dwTFECR = ((pFVSA->byRLETripletCode << 24)
			| (pFVSA->byRLERepeatCode << 16));
		dwTFECR |= ((0x1 << 1) | 1);
		dwTFECR &= TFCTL_DESCRIPTOR_IN_DDR_MASK;

		bRLEOverFLow = true;
		bytesPerPixel = 1;

		dwFetchWidthPixels = get_video_slice_fetch_width(pFVSA->cBuckets);
		dwFetchHeight = dwFetchSize / dwFetchWidthPixels;

		prepare_tfe_descriptor(pDescriptorVirtualAddr, data_phys_out,
				       data_phys_rle, bNotLastEntry, 1, pFVSA->bEnableRLE,
				       dwFetchWidthPixels, bytesPerPixel, dwFetchWidthPixels,
				       dwFetchHeight, 0, bRLEOverFLow, 1);

		VIDEO_DBG("TFE-RLE Control Register value: 0x%x\n", dwTFECR);

		if (sleep_on_tfe_busy(pAstRVAS, dwDescPhysicalAddr, // Descriptor physical Address
			dwTFECR,               // control register value
			dwFetchSize,          // bandwidth limiter value
			&pFVSA->dwSlicedRLESize,       // out:: rle size
			&pFVSA->dwCheckSum
			) == false) {
			ri->rs = GenericError;
			return;
		}

		VIDEO_DBG("Finishing RLE Fetching\n");

		if (pFVSA->dwSlicedRLESize >= pFVSA->dwSlicedSize)
			pFVSA->bRLEFailed = true;
		else
			pFVSA->bRLEFailed = false;
	} // RLE enabled

	memcpy((void *)&dwFetchSize, (void *)&pFVSA->dwSlicedRLESize, 4);
}

void ioctl_fetch_text_data(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	bool bRLEOn = ri->tfm.bEnableRLE;

	ri->rs = SuccessStatus;

	// first time fetch
	on_fetch_text_data(ri, bRLEOn, pAstRVAS);
}

void on_fetch_text_data(struct RvasIoctl *ri, bool bRLEOn, struct AstRVAS *pAstRVAS)
{
	struct Descriptor *pDescriptorAdd;
	struct Descriptor *pDescriptorAddPhys;
	u32 dwScreenOffset = 0x00;
	u32 dwSourceAddress = get_phy_fb_start_address(pAstRVAS);
	u32 dwDestinationAddress;
	bool bRLEOverFlow = false;
	bool bInterrupt = true;
	u32 wFetchLines = 0;
	u8 byCharacterPerLine = 0;
	u16 wFetchWidthInBytes = 0;
	u32 data_phys = 0;
	u32 data_phys_rle = 0;
	u32 data_phys_temp = 0;
	u32 dwCtrlRegValue = 0;
	u32 dwMinBufSize = 0;
	void *desc_virt = NULL;
	u32 desc_phy = 0;
	struct ContextTable *ctx_entry = NULL;

	VIDEO_DBG("Start\n");
	ctx_entry = get_context_entry(ri->rc, pAstRVAS);
	if (ctx_entry) {
		desc_virt = ctx_entry->desc_virt;
		desc_phy = ctx_entry->desc_phy;
	} else {
		ri->rs = InvalidContextHandle;
		return;
	}

	wFetchLines = get_text_mode_fetch_lines(pAstRVAS, ri->vg.wScreenHeight);
	byCharacterPerLine = get_text_mode_character_per_line(pAstRVAS,
							      ri->vg.wScreenWidth);

	data_phys = get_phys_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	data_phys_rle = get_phys_add_rsvd_mem((u32)ri->rmh1, pAstRVAS);

	if (!data_phys || !data_phys_rle) {
		ri->rs = InvalidMemoryHandle;
		dev_err(pAstRVAS->pdev, "Fetch Text: Invalid Memoryhandle\n");
		return;
	}

	dwMinBufSize = (byCharacterPerLine * wFetchLines) << 1;

	if (ri->rmh_mem_size < dwMinBufSize) {
		//either buffer is too small or invalid data in registers
		ri->rs = GenericError;
		dev_err(pAstRVAS->pdev, "Fetch Text: required buffer len:0x%x\n", dwMinBufSize);
		return;
	}
	memset(desc_virt, 0x00, MAX_DESC_SIZE);
	pDescriptorAdd = desc_virt;
	pDescriptorAddPhys = (struct Descriptor *)desc_phy;
	dwDestinationAddress = data_phys;

	// Enable TFE
	dwCtrlRegValue |= 1;
	dwCtrlRegValue &= TFCTL_DESCRIPTOR_IN_DDR_MASK;
	// set up the text alignment
	dwScreenOffset = get_screen_offset(pAstRVAS);
	dwSourceAddress += dwScreenOffset;
	VIDEO_DBG("screen offset:%#x, Source start Addr: %#x\n", dwScreenOffset,
		  dwSourceAddress);
	if (ri->tfm.dpm == AttrMode) { // ATTR and ASCII
		data_phys_temp = data_phys_rle;
		wFetchWidthInBytes = byCharacterPerLine << 3;
		// must fetch both ascii & attr
		VIDEO_DBG("Attribute and ASCII\n");
		prepare_tfe_text_descriptor(desc_virt, dwSourceAddress,
					    data_phys_temp,
					    false, wFetchWidthInBytes, wFetchLines,
					    ri->tfm.dpm, bRLEOverFlow, bInterrupt);
		ri->tfm.dwFetchSize = (byCharacterPerLine * wFetchLines) << 1;
	} else if (ri->tfm.dpm == AsciiOnlyMode) {
		wFetchWidthInBytes = byCharacterPerLine << 3;
		VIDEO_DBG("ASCII Only\n");
		prepare_tfe_text_descriptor(desc_virt, dwSourceAddress,
					    dwDestinationAddress,
					    false, wFetchWidthInBytes, wFetchLines,
					    ri->tfm.dpm, bRLEOverFlow, bInterrupt);
		ri->tfm.dwFetchSize = byCharacterPerLine * wFetchLines;
	} else if (ri->tfm.dpm == FontFetchMode) {
		wFetchWidthInBytes = byCharacterPerLine << 2;
		VIDEO_DBG("Font Only\n");
		prepare_tfe_text_descriptor(desc_virt, dwSourceAddress,
					    dwDestinationAddress,
					    false, wFetchWidthInBytes,
					    wFetchLines + 256,
					    ri->tfm.dpm, bRLEOverFlow, bInterrupt);

		ri->tfm.dwFetchSize = MAX_TEXT_DATA_SIZE;
	}
	dwCtrlRegValue |= 1 << 1; // enabled IRQ
	if (ri->tfm.dpm == AttrMode) {
		if (sleep_on_tfe_text_busy(pAstRVAS, desc_phy, dwCtrlRegValue, // control register value
					   ri->tfm.dwFetchSize,        // bandwidth limitor value
					   &ri->tfm.dwFetchRLESize,        // out:: rle size
					   &ri->tfm.dwCheckSum) == false) {
			dev_err(pAstRVAS->pdev, "Could not sleep_on_tfe_busy for attributes\n");
			ri->rs = GenericError;
			return;
		}
	} else {
		if (sleep_on_tfe_text_busy(pAstRVAS, desc_phy, dwCtrlRegValue,
					   ri->tfm.dwFetchSize, &ri->tfm.dwFetchRLESize,
					   &ri->tfm.dwCheckSum) == false) {
			ri->rs = GenericError;
			dev_err(pAstRVAS->pdev, "Could not sleep_on_tfe_busy for others\n");
			return;
		}
	}

	if (ri->tfm.dpm == AttrMode) {
		//separate ATTR from ATTR+ASCII
		dwSourceAddress = data_phys_temp;
		dwDestinationAddress = data_phys;
		prepare_tfe_descriptor(desc_virt, data_phys_temp, data_phys,
				       false,        //not last entry?
				       1,        //checksum
				       false,        //RLE?
				       byCharacterPerLine,
				       2,        //byBpp,
				       byCharacterPerLine, wFetchLines, TopByteMode,
				       bRLEOverFlow, bInterrupt);

		ri->tfm.dwFetchSize = byCharacterPerLine * wFetchLines;

		dwCtrlRegValue |= 1 << 1;        // enabled IRQ
		if (sleep_on_tfe_text_busy(pAstRVAS, (u32)pDescriptorAddPhys,
					   dwCtrlRegValue, ri->tfm.dwFetchSize,
					   &ri->tfm.dwFetchRLESize, &ri->tfm.dwCheckSum) == false) {
			dev_err(pAstRVAS->pdev, "Could not sleep_on_tfe_busy for attributes # 2\n");
			ri->rs = GenericError;
			return;
		}
	}
	// RLE enabled
	if (bRLEOn) {
		bRLEOverFlow = true;
		dwCtrlRegValue = 1;
		dwCtrlRegValue |= (ri->tfm.byRLETripletCode << 24)
			| (ri->tfm.byRLERepeatCode << 16);
		dwSourceAddress = dwDestinationAddress;
		dwDestinationAddress = data_phys_rle;

		// RLE only
		prepare_tfe_descriptor(pDescriptorAdd, dwSourceAddress,
				       dwDestinationAddress,
				       false,        //not last entry?
				       1,        //checksum
				       bRLEOn,        //RLE?
				       ri->tfm.dwFetchSize / wFetchLines, 1,
				       ri->tfm.dwFetchSize / wFetchLines, wFetchLines,
				       AllBytesMode, bRLEOverFlow, bInterrupt);

		dwCtrlRegValue |= 1 << 1;        // enabled IRQ

		if (sleep_on_tfe_busy(pAstRVAS, (u32)pDescriptorAddPhys, // Descriptor physical Address
				      dwCtrlRegValue,        // control register value
				      ri->tfm.dwFetchSize,        // bandwidth limitor value
				      &ri->tfm.dwFetchRLESize,        // out:: rle size
				      &ri->tfm.dwCheckSum) == false) { // out:: cs size
			dev_err(pAstRVAS->pdev, "Could not sleep_on_tfe_busy for RLE for Text Mode\n");
			ri->rs = GenericError;
			return;
		}     //sleeponTFEBusy
	}
	if (bRLEOn) {
		ri->tfm.bRLEFailed =
			(ri->tfm.dwFetchRLESize < ri->tfm.dwFetchSize) ?
			false : true;
	}
}

u8 get_text_mode_character_per_line(struct AstRVAS *pAstRVAS, u16 wScreenWidth)
{
	u8 byCharPerLine = 0x00;
	u8 byCharWidth = 0;
	u8 byVGASR1 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_SEQ + 0x1));

	byCharWidth = (byVGASR1 & 0x1) ? 8 : 9;
	byCharPerLine = wScreenWidth / byCharWidth;

	return byCharPerLine;
}

u16 get_text_mode_fetch_lines(struct AstRVAS *pAstRVAS, u16 wScreenHeight)
{
	u8 byVGACR9 = readb((void *)(pAstRVAS->grce_reg_base + GRCE_CRTC + 0x9));
	u8 byFontHeight = (byVGACR9 & 0x1F) + 1;
	u16 wFetchLines;

	wFetchLines = wScreenHeight / byFontHeight;

	return wFetchLines;
}

//
// HELPER Functions
//

void prepare_bse_descriptor(struct Descriptor *pDAddress, u32 dwSourceAddress,
			    u32 dwDestAddress, bool bNotLastEntry,
			    u16 wStride, u8 bytesPerPixel,
			    u32 dwFetchWidthPixels, u32 dwFetchHeight,
			    bool bInterrupt)
{
	u16 wDestinationStride;

	// initialize to 0
	pDAddress->dw0General = 0x00;
	pDAddress->dw1FetchWidthLine = 0x00;
	pDAddress->dw2SourceAddr = 0x00;
	pDAddress->dw3DestinationAddr = 0x00;

	wDestinationStride = dwFetchWidthPixels >> 3;

	// initialize to 0
	pDAddress->dw0General = ((wStride * bytesPerPixel) << 16)
		| (wDestinationStride << 8) | (bNotLastEntry << 1) | bInterrupt;
	pDAddress->dw1FetchWidthLine = ((dwFetchHeight - 1) << 16)
		| (dwFetchWidthPixels * bytesPerPixel - 1);
	pDAddress->dw2SourceAddr = dwSourceAddress & 0xfffffffc;
	pDAddress->dw3DestinationAddr = dwDestAddress & 0xfffffffc;

	VIDEO_DBG("After SETTING BSE Descriptor\n");
	VIDEO_DBG("u32 0: 0x%x\n", pDAddress->dw0General);
	VIDEO_DBG("u32 1: 0x%x\n", pDAddress->dw1FetchWidthLine);
	VIDEO_DBG("u32 2: 0x%x\n", pDAddress->dw2SourceAddr);
	VIDEO_DBG("u32 3: 0x%x\n", pDAddress->dw3DestinationAddr);
}

//for descriptor chaining
void prepare_bse_descriptor_2(struct Descriptor *pDAddress, u32 dwSourceAddress,
			      u32 dwDestAddress, bool bNotLastEntry,
			      u16 wStride, u8 bytesPerPixel,
			      u32 dwFetchWidthPixels, u32 dwFetchHeight,
			      bool bInterrupt, u8 byBuckSizeRegIndex)
{
	u16 wDestinationStride;

	// initialize to 0
	pDAddress->dw0General = 0x00;
	pDAddress->dw1FetchWidthLine = 0x00;
	pDAddress->dw2SourceAddr = 0x00;
	pDAddress->dw3DestinationAddr = 0x00;

	wDestinationStride = dwFetchWidthPixels >> 3;

	// initialize to 0
	pDAddress->dw0General = ((wStride * bytesPerPixel) << 16)
		| (wDestinationStride << 8)
		| (byBuckSizeRegIndex << BSE_BUCK_SZ_INDEX_POS)
		| (bNotLastEntry << 1) | bInterrupt;
	pDAddress->dw1FetchWidthLine = ((dwFetchHeight - 1) << 16)
		| (dwFetchWidthPixels * bytesPerPixel - 1);
	pDAddress->dw2SourceAddr = dwSourceAddress & 0xfffffffc;
	pDAddress->dw3DestinationAddr = dwDestAddress & 0xfffffffc;

	VIDEO_DBG("AFter SETTING BSE Descriptor\n");
	VIDEO_DBG("u32 0: 0x%x\n", pDAddress->dw0General);
	VIDEO_DBG("u32 1: 0x%x\n", pDAddress->dw1FetchWidthLine);
	VIDEO_DBG("u32 2: 0x%x\n", pDAddress->dw2SourceAddr);
	VIDEO_DBG("u32 3: 0x%x\n", pDAddress->dw3DestinationAddr);
}

struct BSEAggregateRegister set_up_bse_bucket_2(struct AstRVAS *pAstRVAS, u8 *abyBitIndexes,
						u8 byTotalBucketCount, u8 byBSBytesPerPixel,
						u32 dwFetchWidthPixels, u32 dwFetchHeight,
						u32 dwBucketSizeIndex)
{
	struct BSEAggregateRegister aBSEAR = { 0 };
	u32 addrBSDBS = 0;
	u32 addrBSCR = pAstRVAS->fg_reg_base + BSE_Command_Register;

	if (dwBucketSizeIndex >= BSE_MAX_BUCKET_SIZE_REGS) {
		dev_err(pAstRVAS->pdev, "Video::BSE bucket size index %d too big!",
			dwBucketSizeIndex);
		return aBSEAR;
	}

	addrBSDBS = pAstRVAS->fg_reg_base + BSE_REG_BASE + dwBucketSizeRegOffset[dwBucketSizeIndex];

	// initialize
	memset((void *)&aBSEAR, 0x00, sizeof(struct BSEAggregateRegister));
	aBSEAR = setUp_bse_bucket(abyBitIndexes, byTotalBucketCount,
				  byBSBytesPerPixel, dwFetchWidthPixels, dwFetchHeight);

	writel(aBSEAR.dwBSDBS, (void *)addrBSDBS);
	aBSEAR.dwBSCR |= readl((void *)addrBSCR) & (BSE_ENABLE_MULT_BUCKET_SZS);
	VIDEO_DBG("BSE Bucket size register index %d, [%#x], readback 0x%x\n",
		  dwBucketSizeIndex, aBSEAR.dwBSDBS, readl((void *)addrBSCR));

	return aBSEAR;
}

struct BSEAggregateRegister setUp_bse_bucket(u8 *abyBitIndexes, u8 byTotalBucketCount,
					     u8 byBSBytesPerPixel, u32 dwFetchWidthPixels,
					     u32 dwFetchHeight)
{
	struct BSEAggregateRegister aBSEAR;
	u32 dwSrcBucketSize = MAX_LMEM_BUCKET_SIZE;
	u32 dwDestBucketSize = dwFetchWidthPixels * dwFetchHeight >> 3; //each bucket size
	u8 byRegisterPosition = 0;
	u8 cBucket;

	// initialize
	memset((void *)&aBSEAR, 0x00, sizeof(struct BSEAggregateRegister));

	for (cBucket = 0; cBucket < byTotalBucketCount; cBucket++) {
		if (cBucket < 6) {
			VIDEO_DBG("BUCKET: 0x%x, Bit Position: 0x%x\n", cBucket,
				  abyBitIndexes[cBucket]);
			VIDEO_DBG("BSBPS0 Position: 0x%x\n", byRegisterPosition);
			aBSEAR.adwBSBPS[0] |= abyBitIndexes[cBucket]
				<< byRegisterPosition;

			byRegisterPosition += 5;
		} else if (cBucket >= 6 && cBucket < 12) {
			if (cBucket == 6)
				byRegisterPosition = 0;

			VIDEO_DBG("BUCKET: 0x%x, Bit Position: 0x%x\n", cBucket,
				  abyBitIndexes[cBucket]);
			VIDEO_DBG("BSBPS1 Position: 0x%x\n", byRegisterPosition);
			aBSEAR.adwBSBPS[1] |= abyBitIndexes[cBucket]
				<< byRegisterPosition;
			byRegisterPosition += 5;
		} else {
			if (cBucket == 12)
				byRegisterPosition = 0;

			VIDEO_DBG("BUCKET: 0x%x, Bit Position: 0x%x\n", cBucket,
				  abyBitIndexes[cBucket]);
			VIDEO_DBG("BSBPS2 Position: 0x%x\n", byRegisterPosition);
			aBSEAR.adwBSBPS[2] |= abyBitIndexes[cBucket]
				<< byRegisterPosition;
			byRegisterPosition += 5;
		}
	}

	aBSEAR.dwBSCR = (((byTotalBucketCount - 1) << 8)
			| ((byBSBytesPerPixel - 1) << 4) | (0x0 << 3)
			| (0x1 << 1) | 0x1) & BSCMD_MASK;
	aBSEAR.dwBSDBS = ((dwSrcBucketSize << 24) | dwDestBucketSize)
		& 0xfcfffffc;

	VIDEO_DBG("dwFetchWidthPixels [%#x], dwFetchHeight [%#x]\n",
		  dwFetchWidthPixels, dwFetchHeight);
	VIDEO_DBG("BSE Destination Bucket Size [%#x]\n", dwDestBucketSize);
	VIDEO_DBG("BSE Control [%#x]\n", aBSEAR.dwBSCR);
	VIDEO_DBG("BSE BSDBS [%#x]\n", aBSEAR.dwBSDBS);
	VIDEO_DBG("BSE BSBPS0 [%#x]\n", aBSEAR.adwBSBPS[0]);
	VIDEO_DBG("BSE BSBPS1 [%#x]\n", aBSEAR.adwBSBPS[1]);
	VIDEO_DBG("BSE BSBPS2 [%#x]\n", aBSEAR.adwBSBPS[2]);

	return aBSEAR;
}

void prepare_tfe_descriptor(struct Descriptor *pDAddress, u32 dwSourceAddress,
			    u32 dwDestAddress, bool bNotLastEntry, u8 bCheckSum,
			    bool bEnabledRLE, u16 wStride, u8 bytesPerPixel,
			    u32 dwFetchWidthPixels, u32 dwFetchHeight,
			    enum SelectedByteMode sbm, bool bRLEOverFLow,
			    bool bInterrupt)
{
	enum SkipByteMode skipBM = NoByteSkip;
	enum DataProccessMode dpm = NormalTileMode;
	enum StartBytePosition sbp = StartFromByte0;

	VIDEO_DBG("BEFORE SETTING TFE Descriptor\n");
	// initialize to 0
	pDAddress->dw0General = 0x00;
	pDAddress->dw1FetchWidthLine = 0x00;
	pDAddress->dw2SourceAddr = 0x00;
	pDAddress->dw3DestinationAddr = 0x00;

	if (dwFetchHeight & 0x3)
		dwFetchHeight = ((dwFetchHeight + 3) >> 2) << 2;

	switch (sbm) {
	case AllBytesMode:
		break;

	case LowByteMode:
		dpm = SplitByteMode;
		if (bytesPerPixel == 2)
			skipBM = SkipOneByte;
		else if (bytesPerPixel == 3)
			skipBM = SkipTwoByte;
		else if (bytesPerPixel == 4)
			skipBM = SkipThreeByte;
		break;

	case MiddleByteMode:
		dpm = SplitByteMode;
		if (bytesPerPixel == 2) {
			skipBM = SkipOneByte;
			sbp = StartFromByte1;
		} else if (bytesPerPixel == 3) {
			skipBM = SkipTwoByte;
			sbp = StartFromByte1;
		} else if (bytesPerPixel == 4) {
			skipBM = SkipThreeByte;
			sbp = StartFromByte1;
		}
		break;

	case TopByteMode:
		dpm = SplitByteMode;
		if (bytesPerPixel == 2) {
			skipBM = SkipOneByte;
			sbp = StartFromByte1;
		} else if (bytesPerPixel == 3) {
			skipBM = SkipTwoByte;
			sbp = StartFromByte2;
		} else if (bytesPerPixel == 4) {
			skipBM = SkipThreeByte;
			sbp = StartFromByte2;
		}
		break;

	case PlanarToPackedMode:
		dpm = FourBitPlanarMode;
		break;

	case PackedToPackedMode:
		dpm = FourBitPackedMode;
		break;

	default:
		break;
	}

	if (dwFetchWidthPixels > wStride)
		wStride = dwFetchWidthPixels;

	pDAddress->dw0General = ((wStride * bytesPerPixel) << 16) | (dpm << 13)
		| (sbp << 10) | (skipBM << 8) | (bRLEOverFLow << 7)
		| (bCheckSum << 5) | (bEnabledRLE << 4) | (bNotLastEntry << 1)
		| bInterrupt;
	pDAddress->dw1FetchWidthLine = ((dwFetchHeight - 1) << 16)
		| (dwFetchWidthPixels * bytesPerPixel - 1);
	pDAddress->dw2SourceAddr = dwSourceAddress & 0xfffffffc;
	pDAddress->dw3DestinationAddr = dwDestAddress & 0xfffffffc;

	VIDEO_DBG("After SETTING TFE Descriptor\n");
	VIDEO_DBG("u32 0: 0x%x\n", pDAddress->dw0General);
	VIDEO_DBG("u32 1: 0x%x\n", pDAddress->dw1FetchWidthLine);
	VIDEO_DBG("u32 2: 0x%x\n", pDAddress->dw2SourceAddr);
	VIDEO_DBG("u32 3: 0x%x\n", pDAddress->dw3DestinationAddr);
}

void prepare_tfe_text_descriptor(struct Descriptor *pDAddress, u32 dwSourceAddress,
				 u32 dwDestAddress, bool bEnabledRLE, u32 dwFetchWidth,
				 u32 dwFetchHeight, enum DataProccessMode dpm,
				 bool bRLEOverFLow, bool bInterrupt)
{
	// initialize to 0
	pDAddress->dw0General = 0x00;
	pDAddress->dw1FetchWidthLine = 0x00;
	pDAddress->dw2SourceAddr = 0x00;
	pDAddress->dw3DestinationAddr = 0x00;

	if (dwFetchHeight & 0x3)
		dwFetchHeight = ((dwFetchHeight + 3) >> 2) << 2;

	pDAddress->dw0General = (dwFetchWidth << 16) | (dpm << 13)
		| (bRLEOverFLow << 7) | (1 << 5) | (bEnabledRLE << 4)
		| bInterrupt;
	pDAddress->dw1FetchWidthLine = ((dwFetchHeight - 1) << 16)
		| (dwFetchWidth - 1);
	pDAddress->dw2SourceAddr = dwSourceAddress & 0xfffffffc;
	pDAddress->dw3DestinationAddr = dwDestAddress & 0xfffffffc;

	VIDEO_DBG("u32 0: 0x%x\n", pDAddress->dw0General);
	VIDEO_DBG("u32 1: 0x%x\n", pDAddress->dw1FetchWidthLine);
	VIDEO_DBG("u32 2: 0x%x\n", pDAddress->dw2SourceAddr);
	VIDEO_DBG("u32 3: 0x%x\n", pDAddress->dw3DestinationAddr);
}

void on_fetch_mode_13_data(struct AstRVAS *pAstRVAS, struct RvasIoctl *ri, bool bRLEOn)
{
	struct Descriptor *pDescriptorAdd;
	struct Descriptor *pDescriptorAddPhys;
	u32 dwSourceAddress = get_phy_fb_start_address(pAstRVAS);
	u32 dwDestinationAddress;
	bool bRLEOverFlow = false;
	bool bNotLastEntry = false;
	bool bInterrupt = 1;
	u32 dwFetchHeight = MODE13_HEIGHT;
	u32 dwFetchWidth = MODE13_WIDTH;
	u32 data_phys = 0;
	u32 data_phys_rle = 0;
	u32 dwCtrlRegValue = 0x55AA0080;
	void *desc_virt = NULL;
	u32 desc_phy = 0;
	struct ContextTable *ctx_entry = NULL;

	VIDEO_DBG("Start, bRLEOn: %d\n", bRLEOn);

	ctx_entry = get_context_entry(ri->rc, pAstRVAS);

	if (ctx_entry) {
		desc_virt = ctx_entry->desc_virt;
		desc_phy = ctx_entry->desc_phy;
	} else {
		pr_err("Mode 13: Failed to get context\n");
		ri->rs = InvalidContextHandle;
		return;
	}

	ri->tfm.dwFetchSize = MODE13_HEIGHT * MODE13_WIDTH;

	data_phys = get_phys_add_rsvd_mem((u32)ri->rmh, pAstRVAS);
	data_phys_rle = get_phys_add_rsvd_mem((u32)ri->rmh1, pAstRVAS);

	if (!data_phys || !data_phys_rle) {
		ri->rs = InvalidMemoryHandle;
		dev_err(pAstRVAS->pdev, "Fetch Text: Invalid Memoryhandle\n");
		return;
	}
	if (!data_phys || (bRLEOn && !data_phys_rle)) {
		pr_err("Mode 13: Invalid memory handle\n");
		ri->rs = InvalidMemoryHandle;
		return;
	}

	pDescriptorAdd = desc_virt;
	pDescriptorAddPhys = (struct Descriptor *)desc_phy;

	VIDEO_DBG("\n===========MODE 13 FETCHED DATA===========\n");

	// Enable TFE
	dwCtrlRegValue |= 1;
	dwCtrlRegValue &= TFCTL_DESCRIPTOR_IN_DDR_MASK;
	dwDestinationAddress = data_phys;
	prepare_tfe_descriptor(pDescriptorAdd, dwSourceAddress,
			       dwDestinationAddress,
			       false, //is last entry
			       1,     //checksum
			       false, //No RLE
			       dwFetchWidth,
			       1,		//bytes per pixel
			       dwFetchWidth, dwFetchHeight,
			       PackedToPackedMode, bRLEOverFlow,
			       1);

	dwCtrlRegValue |= 1 << 1; // enabled IRQ

	if (sleep_on_tfe_busy(pAstRVAS, (u32)pDescriptorAddPhys, // Descriptor physical Address
			      dwCtrlRegValue,           // control register value
			      ri->tfm.dwFetchSize,         // bandwidth limitor value
			      &ri->tfm.dwFetchRLESize,     // out:: rle size
			      &ri->tfm.dwCheckSum) == false) {     // out:: cs size
		ri->rs = GenericError;
		return;
	}

	// RLE enabled
	if (bRLEOn) {
		bRLEOverFlow = true;
		dwCtrlRegValue = 1;
		dwCtrlRegValue |= (ri->tfm.byRLETripletCode << 24)
		| (ri->tfm.byRLERepeatCode << 16);
		dwSourceAddress = data_phys;
		dwDestinationAddress = data_phys_rle;
		VIDEO_DBG("RLE is on\n");

		prepare_tfe_descriptor(pDescriptorAdd, dwSourceAddress,
				       dwDestinationAddress,
				       bNotLastEntry,  //not last entry?
				       1,				//checksum
				       bRLEOn,		//RLE?
				       dwFetchWidth, 1, dwFetchWidth, dwFetchHeight,
				       AllBytesMode, bRLEOverFlow, bInterrupt);

		dwCtrlRegValue |= 1 << 1; // enabled IRQ

		if (sleep_on_tfe_busy(pAstRVAS, (u32)pDescriptorAddPhys, // Descriptor physical Address
				      dwCtrlRegValue,           // control register value
				      ri->tfm.dwFetchSize,          // bandwidth limitor value
				      &ri->tfm.dwFetchRLESize,     // out:: rle size
				      &ri->tfm.dwCheckSum) == false) {    // out:: cs size
			ri->rs = GenericError;
			return;
		}    //sleeponTFEBusy
	}

	if (bRLEOn)
		ri->tfm.bRLEFailed =
			(ri->tfm.dwFetchRLESize < ri->tfm.dwFetchSize) ?
			false : true;
}

void ioctl_fetch_mode_13_data(struct RvasIoctl *ri, struct AstRVAS *pAstRVAS)
{
	bool bRLEOn = ri->tfm.bEnableRLE;

	ri->rs = SuccessStatus;

	// first time fetch
	on_fetch_mode_13_data(pAstRVAS, ri, bRLEOn);

	if (ri->rs != SuccessStatus)
		return;

	//if RLE fail. need to TFE without RLE to first buffer
	if (ri->tfm.bEnableRLE & ri->tfm.bRLEFailed) {
		bRLEOn = false;
		on_fetch_mode_13_data(pAstRVAS, ri, bRLEOn);
	}
}

u32 get_phy_fb_start_address(struct AstRVAS *pAstRVAS)
{
	u32 dw_offset = get_screen_offset(pAstRVAS);

	pAstRVAS->FBInfo.dwFBPhysStart = DDR_BASE + pAstRVAS->FBInfo.dwDRAMSize - pAstRVAS->FBInfo.dwVGASize + dw_offset;

	VIDEO_DBG("Frame buffer start address: %#x, dram size: %#x, vga size: %#x\n",
		  pAstRVAS->FBInfo.dwFBPhysStart,
		  pAstRVAS->FBInfo.dwDRAMSize,
		  pAstRVAS->FBInfo.dwVGASize);

	return pAstRVAS->FBInfo.dwFBPhysStart;
}

// Enable Snoop Interrupts and TSE, Disable FIQ
static void enable_tse_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 reg_val = 0;
	u32 reg_addr = pAstRVAS->fg_reg_base
		+ TSE_SnoopCommand_Register_Offset;

	reg_val = readl((void *)reg_addr);
	reg_val |= SNOOP_IRQ_MASK;
	reg_val &= ~SNOOP_FIQ_MASK;

	VIDEO_DBG("Enabled TSE Interrupts[%#X]\n", reg_val);
	writel(reg_val, (void *)reg_addr);
	pAstRVAS->tse_tsicr = TSE_INTR_COUNT;
	reg_addr = pAstRVAS->fg_reg_base
			+ TSE_TileSnoop_Interrupt_Count;
	//set max wait time before interrupt
	writel(pAstRVAS->tse_tsicr, (void *)reg_addr);
}

//disable tse interrupt
static void disable_tse_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 reg_val = 0;
	u32 reg_addr = pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset;

	// Disable Snoop Interrupts and TSE, Disable FIQ
	reg_val = readl((void *)reg_addr);
	VIDEO_DBG("disable interrupt\n");
	reg_val &= ~(SNOOP_IRQ_MASK | SNOOP_FIQ_MASK);
	writel(reg_val, (void *)reg_addr);
}

static void enable_grce_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 reg_val = 0;
	u32 reg_addr = pAstRVAS->grce_reg_base + GRCE_CTL0;

	reg_val = readl((void *)reg_addr);
	reg_val |= GRC_IRQ_MASK;
	writel(reg_val, (void *)reg_addr);
	VIDEO_DBG("Enabled GRC Interrupts[%#X]\n", reg_val);
}

//enable all interrupts
void enable_grce_tse_interrupt(struct AstRVAS *pAstRVAS)
{
	enable_grce_interrupt(pAstRVAS);
	enable_tse_interrupt(pAstRVAS);
}

void disable_grce_tse_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 reg_val = 0;

	VIDEO_DBG("disable_interrupts- grce_reg_base: %#x GRCE_CTL0: %#x\n",
		  pAstRVAS->grce_reg_base, GRCE_CTL0);
	reg_val = readl((void *)(pAstRVAS->grce_reg_base + GRCE_CTL0));
	writel(reg_val & (~GRC_IRQ_MASK), (void *)(pAstRVAS->grce_reg_base + GRCE_CTL0));
	disable_tse_interrupt(pAstRVAS);
}

u32 clear_tse_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 tse_sts = 0;
	u32 tse_tile_status = 0;
	u32 tse_snoop_ctrl = 0;
	u32 tse_ctrl_addr = pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset;

	VIDEO_DBG("clear tse inerrupt");
	tse_sts = readl((void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
	tse_snoop_ctrl = readl((void *)(pAstRVAS->fg_reg_base + TSE_SnoopCommand_Register_Offset));

	if (tse_sts & (TSSTS_TC_SCREEN0 | TSSTS_TC_SCREEN1)) {
		if (tse_sts & TSSTS_TC_SCREEN0) {
			VIDEO_DBG("Snoop** Update Screen 0\n");
			 // clear interrupt and switch to screen 1
			tse_snoop_ctrl |= TSCMD_SCREEN_OWNER;
			writel(tse_sts, (void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
			writel(tse_snoop_ctrl, (void *)tse_ctrl_addr);

		} else if (tse_sts & TSSTS_TC_SCREEN1) {
			VIDEO_DBG("Snoop** Update Screen 1\n");
			tse_snoop_ctrl &= ~TSCMD_SCREEN_OWNER; // snap shutter
			// clear status
			writel(tse_sts, (void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
			 // clear interrupt and switch to screen 1
			writel(tse_snoop_ctrl, (void *)tse_ctrl_addr);
		}
		// read clear interrupt
		tse_tile_status = readl((void *)(pAstRVAS->fg_reg_base
				+ TSE_TileCount_Register_Offset));

		if (tse_sts & TSSTS_FIFO_OVFL) {
			//need to send full frame
			dev_err(pAstRVAS->pdev, "TSE snoop fifo overflow\n");
			writel(TSSTS_FIFO_OVFL, (void *)(pAstRVAS->fg_reg_base + TSE_Status_Register_Offset));
			memset((void *)pAstRVAS->accrued_sm, 0xff, sizeof(pAstRVAS->accrued_sm));
			memset((void *)&pAstRVAS->accrued_sa, 0xff,
			       sizeof(pAstRVAS->accrued_sa));
		} else {
			get_snoop_map_data(pAstRVAS);
		}
	}
	return tse_sts;
}

// LDMA interrupt
bool clear_ldma_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 ldma_sts = 0;

	ldma_sts = readl((void *)(pAstRVAS->fg_reg_base + LDMA_Status_Register));

	if (ldma_sts & 0x02) {
		//VIDEO_DBG("Got a LDMA interrupt\n");
		// write 1 to clear the interrupt
		writel(0x2, (void *)(pAstRVAS->fg_reg_base + LDMA_Status_Register));
		return true;
	}
	return false;
}

bool clear_tfe_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 tfe_sts = 0;

	tfe_sts = readl((void *)(pAstRVAS->fg_reg_base + TFE_Status_Register));

	if (tfe_sts & 0x02) {
		// VIDEO_DBG("Debug: TFSTS Interrupt is triggered\n");
		writel(0x2, (void *)(pAstRVAS->fg_reg_base + TFE_Status_Register));
		return true;
	}
	return false;
}

bool clear_bse_interrupt(struct AstRVAS *pAstRVAS)
{
	u32 bse_sts = 0;

	bse_sts = readl((void *)(pAstRVAS->fg_reg_base + BSE_Status_Register));

	if (bse_sts & 0x02) {
		writel(0x2, (void *)(pAstRVAS->fg_reg_base + BSE_Status_Register));
		return true;
	}
	return false;
}

void setup_lmem(struct AstRVAS *pAstRVAS)
{
	writel(0x0, (void *)(pAstRVAS->fg_reg_base + LMEM_BASE_REG_3));
	writel(0x2000, (void *)(pAstRVAS->fg_reg_base + LMEM_LIMIT_REG_3));
	writel(0x9c89c8, (void *)(pAstRVAS->fg_reg_base + LMEM11_P0));
	writel(0x9c89c8, (void *)(pAstRVAS->fg_reg_base + LMEM12_P0));
	writel(0xf3cf3c, (void *)(pAstRVAS->fg_reg_base + LMEM11_P1));
	writel(0x067201, (void *)(pAstRVAS->fg_reg_base + LMEM11_P2));
	writel(0x00F3CF3C, (void *)(pAstRVAS->fg_reg_base + LMEM10_P1));
	writel(0x00067201, (void *)(pAstRVAS->fg_reg_base + LMEM10_P2));
}

bool host_suspended(struct AstRVAS *pAstRVAS)
{
	u32 GRCE18 = readl((void *)(pAstRVAS->grce_reg_base + GRCE_ATTR_VGAIR0_OFFSET));

	// VGAER is GRCE19
	// VGAER bit[0]:0 - vga disabled (host suspended)
	// 1 - vga enabled
	VIDEO_DBG("GRCE18:%#x\n", GRCE18);
	if (GRCE18 & 0x100)
		return false;
	else
		return true;
}

