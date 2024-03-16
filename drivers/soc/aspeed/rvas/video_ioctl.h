/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * This file is part of the ASPEED Linux Device Driver for ASPEED Baseboard Management Controller.
 * Refer to the README file included with this package for driver version and adapter compatibility.
 *
 * Copyright (C) 2019-2021 ASPEED Technology Inc. All rights reserved.
 *
 */

#ifndef _VIDEO_IOCTL_H
#define _VIDEO_IOCTL_H

#include <linux/types.h>

#define RVAS_MAGIC				('b')
#define CMD_IOCTL_TURN_LOCAL_MONITOR_ON		_IOR(RVAS_MAGIC, IOCTL_TURN_LOCAL_MONITOR_ON, struct RvasIoctl)
#define CMD_IOCTL_TURN_LOCAL_MONITOR_OFF	_IOR(RVAS_MAGIC, IOCTL_TURN_LOCAL_MONITOR_OFF, struct RvasIoctl)
#define CMD_IOCTL_IS_LOCAL_MONITOR_ENABLED	_IOR(RVAS_MAGIC, IOCTL_IS_LOCAL_MONITOR_ENABLED, struct RvasIoctl)
#define CMD_IOCTL_GET_VIDEO_GEOMETRY		_IOWR(RVAS_MAGIC, IOCTL_GET_VIDEO_GEOMETRY, struct RvasIoctl)
#define CMD_IOCTL_WAIT_FOR_VIDEO_EVENT		_IOWR(RVAS_MAGIC, IOCTL_WAIT_FOR_VIDEO_EVENT, struct RvasIoctl)
#define CMD_IOCTL_GET_GRC_REGIESTERS		_IOWR(RVAS_MAGIC, IOCTL_GET_GRC_REGIESTERS, struct RvasIoctl)
#define CMD_IOCTL_READ_SNOOP_MAP		_IOWR(RVAS_MAGIC, IOCTL_READ_SNOOP_MAP, struct RvasIoctl)
#define CMD_IOCTL_READ_SNOOP_AGGREGATE		_IOWR(RVAS_MAGIC, IOCTL_READ_SNOOP_AGGREGATE, struct RvasIoctl)
#define CMD_IOCTL_FETCH_VIDEO_TILES		_IOWR(RVAS_MAGIC, IOCTL_FETCH_VIDEO_TILES, struct RvasIoctl)
#define CMD_IOCTL_FETCH_VIDEO_SLICES		_IOWR(RVAS_MAGIC, IOCTL_FETCH_VIDEO_SLICES, struct RvasIoctl)
#define CMD_IOCTL_RUN_LENGTH_ENCODE_DATA	_IOWR(RVAS_MAGIC, IOCTL_RUN_LENGTH_ENCODE_DATA, struct RvasIoctl)
#define CMD_IOCTL_FETCH_TEXT_DATA		_IOWR(RVAS_MAGIC, IOCTL_FETCH_TEXT_DATA, struct RvasIoctl)
#define CMD_IOCTL_FETCH_MODE13_DATA		_IOWR(RVAS_MAGIC, IOCTL_FETCH_MODE13_DATA, struct RvasIoctl)
#define CMD_IOCTL_NEW_CONTEXT			_IOWR(RVAS_MAGIC, IOCTL_NEW_CONTEXT, struct RvasIoctl)
#define CMD_IOCTL_DEL_CONTEXT			_IOWR(RVAS_MAGIC, IOCTL_DEL_CONTEXT, struct RvasIoctl)
#define CMD_IOCTL_ALLOC				_IOWR(RVAS_MAGIC, IOCTL_ALLOC, struct RvasIoctl)
#define CMD_IOCTL_FREE				_IOWR(RVAS_MAGIC, IOCTL_FREE, struct RvasIoctl)
#define CMD_IOCTL_SET_TSE_COUNTER		_IOWR(RVAS_MAGIC, IOCTL_SET_TSE_COUNTER, struct RvasIoctl)
#define CMD_IOCTL_GET_TSE_COUNTER		_IOWR(RVAS_MAGIC, IOCTL_GET_TSE_COUNTER, struct RvasIoctl)
#define CMD_IOCTL_VIDEO_ENGINE_RESET		_IOWR(RVAS_MAGIC, IOCTL_VIDEO_ENGINE_RESET, struct RvasIoctl)
//jpeg
#define CMD_IOCTL_SET_VIDEO_ENGINE_CONFIG	_IOW(RVAS_MAGIC, IOCTL_SET_VIDEO_ENGINE_CONFIG, struct VideoConfig*)
#define CMD_IOCTL_GET_VIDEO_ENGINE_CONFIG	_IOW(RVAS_MAGIC, IOCTL_GET_VIDEO_ENGINE_CONFIG, struct VideoConfig*)
#define CMD_IOCTL_GET_VIDEO_ENGINE_DATA		_IOWR(RVAS_MAGIC, IOCTL_GET_VIDEO_ENGINE_DATA, struct MultiJpegConfig*)

enum  HARD_WARE_ENGINE_IOCTL {
	IOCTL_TURN_LOCAL_MONITOR_ON = 20, //REMOTE VIDEO GENERAL IOCTL
	IOCTL_TURN_LOCAL_MONITOR_OFF,
	IOCTL_IS_LOCAL_MONITOR_ENABLED,

	IOCTL_GET_VIDEO_GEOMETRY = 40, // REMOTE VIDEO
	IOCTL_WAIT_FOR_VIDEO_EVENT,
	IOCTL_GET_GRC_REGIESTERS,
	IOCTL_READ_SNOOP_MAP,
	IOCTL_READ_SNOOP_AGGREGATE,
	IOCTL_FETCH_VIDEO_TILES,
	IOCTL_FETCH_VIDEO_SLICES,
	IOCTL_RUN_LENGTH_ENCODE_DATA,
	IOCTL_FETCH_TEXT_DATA,
	IOCTL_FETCH_MODE13_DATA,
	IOCTL_NEW_CONTEXT,
	IOCTL_DEL_CONTEXT,
	IOCTL_ALLOC,
	IOCTL_FREE,
	IOCTL_SET_TSE_COUNTER,
	IOCTL_GET_TSE_COUNTER,
	IOCTL_VIDEO_ENGINE_RESET,
	IOCTL_SET_VIDEO_ENGINE_CONFIG,
	IOCTL_GET_VIDEO_ENGINE_CONFIG,
	IOCTL_GET_VIDEO_ENGINE_DATA,
};

enum GraphicsModeType {
	InvalidMode = 0, TextMode = 1, VGAGraphicsMode = 2, AGAGraphicsMode = 3
};

enum RVASStatus {
	SuccessStatus = 0,
	GenericError = 1,
	MemoryAllocError = 2,
	InvalidMemoryHandle = 3,
	CannotMapMemory = 4,
	CannotUnMapMemory = 5,
	TimedOut = 6,
	InvalidContextHandle = 7,
	CaptureTimedOut = 8,
	CompressionTimedOut = 9,
	HostSuspended
};

enum SelectedByteMode {
	AllBytesMode = 0,
	SkipMode = 1,
	PlanarToPackedMode,
	PackedToPackedMode,
	LowByteMode,
	MiddleByteMode,
	TopByteMode
};

enum DataProccessMode {
	NormalTileMode = 0,
	FourBitPlanarMode = 1,
	FourBitPackedMode = 2,
	AttrMode = 3,
	AsciiOnlyMode = 4,
	FontFetchMode = 5,
	SplitByteMode = 6
};

enum ResetEngineMode {
	ResetAll = 0,
	ResetRvasEngine = 1,
	ResetVeEngine = 2
};

struct VideoGeometry {
	u16 wScreenWidth;
	u16 wScreenHeight;
	u16 wStride;
	u8 byBitsPerPixel;
	u8 byModeID;
	enum GraphicsModeType gmt;
};

struct EventMap {
	u32 bPaletteChanged :1;
	u32 bATTRChanged :1;
	u32 bSEQChanged :1;
	u32 bGCTLChanged :1;
	u32 bCRTCChanged :1;
	u32 bCRTCEXTChanged :1;
	u32 bPLTRAMChanged :1;
	u32 bXCURCOLChanged :1;
	u32 bXCURCTLChanged :1;
	u32 bXCURPOSChanged :1;
	u32 bDoorbellA :1;
	u32 bDoorbellB :1;
	u32 bGeometryChanged :1;
	u32 bSnoopChanged :1;
	u32 bTextFontChanged :1;
	u32 bTextATTRChanged :1;
	u32 bTextASCIIChanged :1;
};

struct FetchMap {
	//in parameters
	bool bEnableRLE;
	u8 bTextAlignDouble; // 0 - 8 byte, 1 - 16 byte
	u8 byRLETripletCode;
	u8 byRLERepeatCode;
	enum DataProccessMode dpm;
	//out parameters
	u32 dwFetchSize;
	u32 dwFetchRLESize;
	u32 dwCheckSum;
	bool bRLEFailed;
	u8 rsvd[3];
};

struct SnoopAggregate {
	u64 qwRow;
	u64 qwCol;
};

struct FetchRegion {
	u16 wTopY;
	u16 wLeftX;
	u16 wBottomY;
	u16 wRightX;
};

struct FetchOperation {
	struct FetchRegion fr;
	enum SelectedByteMode sbm;
	u32 dwFetchSize;
	u32 dwFetchRLESize;
	u32 dwCheckSum;
	bool bRLEFailed;
	bool bEnableRLE;
	u8 byRLETripletCode;
	u8 byRLERepeatCode;
	u8 byVGATextAlignment; //0-8bytes, 1-16bytes.
	u8 rsvd[3];
};

struct FetchVideoTilesArg {
	struct VideoGeometry vg;
	u32 dwTotalOutputSize;
	u32 cfo;
	struct FetchOperation pfo[4];
};

struct FetchVideoSlicesArg {
	struct VideoGeometry vg;
	u32 dwSlicedSize;
	u32 dwSlicedRLESize;
	u32 dwCheckSum;
	bool bEnableRLE;
	bool bRLEFailed;
	u8 byRLETripletCode;
	u8 byRLERepeatCode;
	u8 cBuckets;
	u8 rsvd[3];
	u8 abyBitIndexes[24];
	u32 cfr;
	struct FetchRegion pfr[4];
};

struct RVASBuffer {
	void *pv;
	size_t cb;
};

struct RvasIoctl {
	enum RVASStatus rs;
	void *rc;
	struct RVASBuffer rvb;
	void *rmh;
	void *rmh1;
	void *rmh2;
	u32 rmh_mem_size;
	u32 rmh1_mem_size;
	u32 rmh2_mem_size;
	struct VideoGeometry vg;
	struct EventMap em;
	struct SnoopAggregate sa;
	union {
		u32 tse_counter;
		u32 req_mem_size;
		u32 encode;
		u32 time_out;
	};
	u32 rle_len;  // RLE Length
	u32 rle_checksum;
	struct FetchMap tfm;
	u8 flag;
	u8 lms;
	u8 resetMode;
	u8 rsvd;
};

//
// Video Engine
//

#define MAX_MULTI_FRAME_CT (32)

struct VideoConfig {
	u8 engine;					//0: engine 0 - normal engine, engine 1 - VM legacy engine
	u8 compression_mode;	//0:DCT, 1:DCT_VQ mix VQ-2 color, 2:DCT_VQ mix VQ-4 color		9:
	u8 compression_format;	//0:ASPEED 1:JPEG
	u8 capture_format;		//0:CCIR601-2 YUV, 1:JPEG YUV, 2:RGB for ASPEED mode only, 3:Gray
	u8 rc4_enable;				//0:disable 1:enable
	u8 YUV420_mode;			//0:YUV444, 1:YUV420
	u8 Visual_Lossless;
	u8 Y_JPEGTableSelector;
	u8 AdvanceTableSelector;
	u8 AutoMode;
	u8 rsvd[2];
	enum RVASStatus rs;
};

struct MultiJpegFrame {
	u32 dwSizeInBytes;			// Image size in bytes
	u32 dwOffsetInBytes;			// Offset in bytes
	u16 wXPixels;					// In: X coordinate
	u16 wYPixels;					// In: Y coordinate
	u16 wWidthPixels;				// In: Width for Fetch
	u16 wHeightPixels;			// In: Height for Fetch
};

struct MultiJpegConfig {
	unsigned char multi_jpeg_frames;				// frame count
	struct MultiJpegFrame frame[MAX_MULTI_FRAME_CT];	// The Multi Frames
	void *aStreamHandle;
	enum RVASStatus rs;
};

#endif // _VIDEO_IOCTL_H
