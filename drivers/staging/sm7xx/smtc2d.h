/*
 * Silicon Motion SM712 2D drawing engine functions.
 *
 * Copyright (C) 2006 Silicon Motion Technology Corp.
 * Author: Ge Wang, gewang@siliconmotion.com
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin, wuzj@lemote.com
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef NULL
#define NULL	 0
#endif

/* Internal macros */

#define _F_START(f)		(0 ? f)
#define _F_END(f)		(1 ? f)
#define _F_SIZE(f)		(1 + _F_END(f) - _F_START(f))
#define _F_MASK(f)		(((1ULL << _F_SIZE(f)) - 1) << _F_START(f))
#define _F_NORMALIZE(v, f)	(((v) & _F_MASK(f)) >> _F_START(f))
#define _F_DENORMALIZE(v, f)	(((v) << _F_START(f)) & _F_MASK(f))

/* Global macros */

#define FIELD_GET(x, reg, field) \
( \
    _F_NORMALIZE((x), reg ## _ ## field) \
)

#define FIELD_SET(x, reg, field, value) \
( \
    (x & ~_F_MASK(reg ## _ ## field)) \
    | _F_DENORMALIZE(reg ## _ ## field ## _ ## value, reg ## _ ## field) \
)

#define FIELD_VALUE(x, reg, field, value) \
( \
    (x & ~_F_MASK(reg ## _ ## field)) \
    | _F_DENORMALIZE(value, reg ## _ ## field) \
)

#define FIELD_CLEAR(reg, field) \
( \
    ~_F_MASK(reg ## _ ## field) \
)

/* Field Macros                        */

#define FIELD_START(field)	(0 ? field)
#define FIELD_END(field)	(1 ? field)
#define FIELD_SIZE(field) \
	(1 + FIELD_END(field) - FIELD_START(field))

#define FIELD_MASK(field) \
	(((1 << (FIELD_SIZE(field)-1)) \
	| ((1 << (FIELD_SIZE(field)-1)) - 1)) \
	<< FIELD_START(field))

#define FIELD_NORMALIZE(reg, field) \
	(((reg) & FIELD_MASK(field)) >> FIELD_START(field))

#define FIELD_DENORMALIZE(field, value) \
	(((value) << FIELD_START(field)) & FIELD_MASK(field))

#define FIELD_INIT(reg, field, value) \
	FIELD_DENORMALIZE(reg ## _ ## field, \
		reg ## _ ## field ## _ ## value)

#define FIELD_INIT_VAL(reg, field, value) \
	(FIELD_DENORMALIZE(reg ## _ ## field, value))

#define FIELD_VAL_SET(x, r, f, v) ({ \
	x = (x & ~FIELD_MASK(r ## _ ## f)) \
	| FIELD_DENORMALIZE(r ## _ ## f, r ## _ ## f ## _ ## v) \
})

#define RGB(r, g, b)	((unsigned long)(((r) << 16) | ((g) << 8) | (b)))

/* Transparent info definition */
typedef struct {
	unsigned long match;	/* Matching pixel is OPAQUE/TRANSPARENT */
	unsigned long select;	/* Transparency controlled by SRC/DST */
	unsigned long control;	/* ENABLE/DISABLE transparency */
	unsigned long color;	/* Transparent color */
} Transparent, *pTransparent;

#define PIXEL_DEPTH_1_BP	0	/* 1 bit per pixel */
#define PIXEL_DEPTH_8_BPP	1	/* 8 bits per pixel */
#define PIXEL_DEPTH_16_BPP	2	/* 16 bits per pixel */
#define PIXEL_DEPTH_32_BPP	3	/* 32 bits per pixel */
#define PIXEL_DEPTH_YUV422	8	/* 16 bits per pixel YUV422 */
#define PIXEL_DEPTH_YUV420	9	/* 16 bits per pixel YUV420 */

#define PATTERN_WIDTH		8
#define PATTERN_HEIGHT		8

#define	TOP_TO_BOTTOM		0
#define	BOTTOM_TO_TOP		1
#define RIGHT_TO_LEFT		BOTTOM_TO_TOP
#define LEFT_TO_RIGHT		TOP_TO_BOTTOM

/* Constants used in Transparent structure */
#define MATCH_OPAQUE		0x00000000
#define MATCH_TRANSPARENT	0x00000400
#define SOURCE			0x00000000
#define DESTINATION		0x00000200

/* 2D registers. */

#define	DE_SOURCE			0x000000
#define	DE_SOURCE_WRAP			31 : 31
#define	DE_SOURCE_WRAP_DISABLE		0
#define	DE_SOURCE_WRAP_ENABLE		1
#define	DE_SOURCE_X_K1			29 : 16
#define	DE_SOURCE_Y_K2			15 : 0

#define	DE_DESTINATION			0x000004
#define	DE_DESTINATION_WRAP		31 : 31
#define	DE_DESTINATION_WRAP_DISABLE	0
#define	DE_DESTINATION_WRAP_ENABLE	1
#define	DE_DESTINATION_X		28 : 16
#define	DE_DESTINATION_Y		15 : 0

#define	DE_DIMENSION			0x000008
#define	DE_DIMENSION_X			28 : 16
#define	DE_DIMENSION_Y_ET		15 : 0

#define	DE_CONTROL			0x00000C
#define	DE_CONTROL_STATUS		31 : 31
#define	DE_CONTROL_STATUS_STOP		0
#define	DE_CONTROL_STATUS_START		1
#define	DE_CONTROL_PATTERN		30 : 30
#define	DE_CONTROL_PATTERN_MONO		0
#define	DE_CONTROL_PATTERN_COLOR	1
#define	DE_CONTROL_UPDATE_DESTINATION_X		29 : 29
#define	DE_CONTROL_UPDATE_DESTINATION_X_DISABLE	0
#define	DE_CONTROL_UPDATE_DESTINATION_X_ENABLE	1
#define	DE_CONTROL_QUICK_START			28 : 28
#define	DE_CONTROL_QUICK_START_DISABLE		0
#define	DE_CONTROL_QUICK_START_ENABLE		1
#define	DE_CONTROL_DIRECTION			27 : 27
#define	DE_CONTROL_DIRECTION_LEFT_TO_RIGHT	0
#define	DE_CONTROL_DIRECTION_RIGHT_TO_LEFT	1
#define	DE_CONTROL_MAJOR			26 : 26
#define	DE_CONTROL_MAJOR_X			0
#define	DE_CONTROL_MAJOR_Y			1
#define	DE_CONTROL_STEP_X			25 : 25
#define	DE_CONTROL_STEP_X_POSITIVE		1
#define	DE_CONTROL_STEP_X_NEGATIVE		0
#define	DE_CONTROL_STEP_Y			24 : 24
#define	DE_CONTROL_STEP_Y_POSITIVE		1
#define	DE_CONTROL_STEP_Y_NEGATIVE		0
#define	DE_CONTROL_STRETCH			23 : 23
#define	DE_CONTROL_STRETCH_DISABLE		0
#define	DE_CONTROL_STRETCH_ENABLE		1
#define	DE_CONTROL_HOST				22 : 22
#define	DE_CONTROL_HOST_COLOR			0
#define	DE_CONTROL_HOST_MONO			1
#define	DE_CONTROL_LAST_PIXEL			21 : 21
#define	DE_CONTROL_LAST_PIXEL_OFF		0
#define	DE_CONTROL_LAST_PIXEL_ON		1
#define	DE_CONTROL_COMMAND			20 : 16
#define	DE_CONTROL_COMMAND_BITBLT		0
#define	DE_CONTROL_COMMAND_RECTANGLE_FILL	1
#define	DE_CONTROL_COMMAND_DE_TILE		2
#define	DE_CONTROL_COMMAND_TRAPEZOID_FILL	3
#define	DE_CONTROL_COMMAND_ALPHA_BLEND		4
#define	DE_CONTROL_COMMAND_RLE_STRIP		5
#define	DE_CONTROL_COMMAND_SHORT_STROKE		6
#define	DE_CONTROL_COMMAND_LINE_DRAW		7
#define	DE_CONTROL_COMMAND_HOST_WRITE		8
#define	DE_CONTROL_COMMAND_HOST_READ		9
#define	DE_CONTROL_COMMAND_HOST_WRITE_BOTTOM_UP	10
#define	DE_CONTROL_COMMAND_ROTATE		11
#define	DE_CONTROL_COMMAND_FONT			12
#define	DE_CONTROL_COMMAND_TEXTURE_LOAD		15
#define	DE_CONTROL_ROP_SELECT			15 : 15
#define	DE_CONTROL_ROP_SELECT_ROP3		0
#define	DE_CONTROL_ROP_SELECT_ROP2		1
#define	DE_CONTROL_ROP2_SOURCE			14 : 14
#define	DE_CONTROL_ROP2_SOURCE_BITMAP		0
#define	DE_CONTROL_ROP2_SOURCE_PATTERN		1
#define	DE_CONTROL_MONO_DATA			13 : 12
#define	DE_CONTROL_MONO_DATA_NOT_PACKED		0
#define	DE_CONTROL_MONO_DATA_8_PACKED		1
#define	DE_CONTROL_MONO_DATA_16_PACKED		2
#define	DE_CONTROL_MONO_DATA_32_PACKED		3
#define	DE_CONTROL_REPEAT_ROTATE		11 : 11
#define	DE_CONTROL_REPEAT_ROTATE_DISABLE	0
#define	DE_CONTROL_REPEAT_ROTATE_ENABLE		1
#define	DE_CONTROL_TRANSPARENCY_MATCH		10 : 10
#define	DE_CONTROL_TRANSPARENCY_MATCH_OPAQUE		0
#define	DE_CONTROL_TRANSPARENCY_MATCH_TRANSPARENT	1
#define	DE_CONTROL_TRANSPARENCY_SELECT			9 : 9
#define	DE_CONTROL_TRANSPARENCY_SELECT_SOURCE		0
#define	DE_CONTROL_TRANSPARENCY_SELECT_DESTINATION	1
#define	DE_CONTROL_TRANSPARENCY				8 : 8
#define	DE_CONTROL_TRANSPARENCY_DISABLE			0
#define	DE_CONTROL_TRANSPARENCY_ENABLE			1
#define	DE_CONTROL_ROP					7 : 0

/* Pseudo fields. */

#define	DE_CONTROL_SHORT_STROKE_DIR			27 : 24
#define	DE_CONTROL_SHORT_STROKE_DIR_225			0
#define	DE_CONTROL_SHORT_STROKE_DIR_135			1
#define	DE_CONTROL_SHORT_STROKE_DIR_315			2
#define	DE_CONTROL_SHORT_STROKE_DIR_45			3
#define	DE_CONTROL_SHORT_STROKE_DIR_270			4
#define	DE_CONTROL_SHORT_STROKE_DIR_90			5
#define	DE_CONTROL_SHORT_STROKE_DIR_180			8
#define	DE_CONTROL_SHORT_STROKE_DIR_0			10
#define	DE_CONTROL_ROTATION				25 : 24
#define	DE_CONTROL_ROTATION_0				0
#define	DE_CONTROL_ROTATION_270				1
#define	DE_CONTROL_ROTATION_90				2
#define	DE_CONTROL_ROTATION_180				3

#define	DE_PITCH					0x000010
#define	DE_PITCH_DESTINATION				28 : 16
#define	DE_PITCH_SOURCE					12 : 0

#define	DE_FOREGROUND					0x000014
#define	DE_FOREGROUND_COLOR				31 : 0

#define	DE_BACKGROUND					0x000018
#define	DE_BACKGROUND_COLOR				31 : 0

#define	DE_STRETCH_FORMAT				0x00001C
#define	DE_STRETCH_FORMAT_PATTERN_XY			30 : 30
#define	DE_STRETCH_FORMAT_PATTERN_XY_NORMAL		0
#define	DE_STRETCH_FORMAT_PATTERN_XY_OVERWRITE		1
#define	DE_STRETCH_FORMAT_PATTERN_Y			29 : 27
#define	DE_STRETCH_FORMAT_PATTERN_X			25 : 23
#define	DE_STRETCH_FORMAT_PIXEL_FORMAT			21 : 20
#define	DE_STRETCH_FORMAT_PIXEL_FORMAT_8		0
#define	DE_STRETCH_FORMAT_PIXEL_FORMAT_16		1
#define	DE_STRETCH_FORMAT_PIXEL_FORMAT_24		3
#define	DE_STRETCH_FORMAT_PIXEL_FORMAT_32		2
#define	DE_STRETCH_FORMAT_ADDRESSING			19 : 16
#define	DE_STRETCH_FORMAT_ADDRESSING_XY			0
#define	DE_STRETCH_FORMAT_ADDRESSING_LINEAR		15
#define	DE_STRETCH_FORMAT_SOURCE_HEIGHT			11 : 0

#define	DE_COLOR_COMPARE				0x000020
#define	DE_COLOR_COMPARE_COLOR				23 : 0

#define	DE_COLOR_COMPARE_MASK				0x000024
#define	DE_COLOR_COMPARE_MASK_MASKS			23 : 0

#define	DE_MASKS					0x000028
#define	DE_MASKS_BYTE_MASK				31 : 16
#define	DE_MASKS_BIT_MASK				15 : 0

#define	DE_CLIP_TL					0x00002C
#define	DE_CLIP_TL_TOP					31 : 16
#define	DE_CLIP_TL_STATUS				13 : 13
#define	DE_CLIP_TL_STATUS_DISABLE			0
#define	DE_CLIP_TL_STATUS_ENABLE			1
#define	DE_CLIP_TL_INHIBIT				12 : 12
#define	DE_CLIP_TL_INHIBIT_OUTSIDE			0
#define	DE_CLIP_TL_INHIBIT_INSIDE			1
#define	DE_CLIP_TL_LEFT					11 : 0

#define	DE_CLIP_BR					0x000030
#define	DE_CLIP_BR_BOTTOM				31 : 16
#define	DE_CLIP_BR_RIGHT				12 : 0

#define	DE_MONO_PATTERN_LOW				0x000034
#define	DE_MONO_PATTERN_LOW_PATTERN			31 : 0

#define	DE_MONO_PATTERN_HIGH				0x000038
#define	DE_MONO_PATTERN_HIGH_PATTERN			31 : 0

#define	DE_WINDOW_WIDTH					0x00003C
#define	DE_WINDOW_WIDTH_DESTINATION			28 : 16
#define	DE_WINDOW_WIDTH_SOURCE				12 : 0

#define	DE_WINDOW_SOURCE_BASE				0x000040
#define	DE_WINDOW_SOURCE_BASE_EXT			27 : 27
#define	DE_WINDOW_SOURCE_BASE_EXT_LOCAL			0
#define	DE_WINDOW_SOURCE_BASE_EXT_EXTERNAL		1
#define	DE_WINDOW_SOURCE_BASE_CS			26 : 26
#define	DE_WINDOW_SOURCE_BASE_CS_0			0
#define	DE_WINDOW_SOURCE_BASE_CS_1			1
#define	DE_WINDOW_SOURCE_BASE_ADDRESS			25 : 0

#define	DE_WINDOW_DESTINATION_BASE			0x000044
#define	DE_WINDOW_DESTINATION_BASE_EXT			27 : 27
#define	DE_WINDOW_DESTINATION_BASE_EXT_LOCAL		0
#define	DE_WINDOW_DESTINATION_BASE_EXT_EXTERNAL		1
#define	DE_WINDOW_DESTINATION_BASE_CS			26 : 26
#define	DE_WINDOW_DESTINATION_BASE_CS_0			0
#define	DE_WINDOW_DESTINATION_BASE_CS_1			1
#define	DE_WINDOW_DESTINATION_BASE_ADDRESS		25 : 0

#define	DE_ALPHA					0x000048
#define	DE_ALPHA_VALUE					7 : 0

#define	DE_WRAP						0x00004C
#define	DE_WRAP_X					31 : 16
#define	DE_WRAP_Y					15 : 0

#define	DE_STATUS					0x000050
#define	DE_STATUS_CSC					1 : 1
#define	DE_STATUS_CSC_CLEAR				0
#define	DE_STATUS_CSC_NOT_ACTIVE			0
#define	DE_STATUS_CSC_ACTIVE				1
#define	DE_STATUS_2D					0 : 0
#define	DE_STATUS_2D_CLEAR				0
#define	DE_STATUS_2D_NOT_ACTIVE				0
#define	DE_STATUS_2D_ACTIVE				1

/* Color Space Conversion registers. */

#define	CSC_Y_SOURCE_BASE				0x0000C8
#define	CSC_Y_SOURCE_BASE_EXT				27 : 27
#define	CSC_Y_SOURCE_BASE_EXT_LOCAL			0
#define	CSC_Y_SOURCE_BASE_EXT_EXTERNAL			1
#define	CSC_Y_SOURCE_BASE_CS				26 : 26
#define	CSC_Y_SOURCE_BASE_CS_0				0
#define	CSC_Y_SOURCE_BASE_CS_1				1
#define	CSC_Y_SOURCE_BASE_ADDRESS			25 : 0

#define	CSC_CONSTANTS					0x0000CC
#define	CSC_CONSTANTS_Y					31 : 24
#define	CSC_CONSTANTS_R					23 : 16
#define	CSC_CONSTANTS_G					15 : 8
#define	CSC_CONSTANTS_B					7 : 0

#define	CSC_Y_SOURCE_X					0x0000D0
#define	CSC_Y_SOURCE_X_INTEGER				26 : 16
#define	CSC_Y_SOURCE_X_FRACTION				15 : 3

#define	CSC_Y_SOURCE_Y					0x0000D4
#define	CSC_Y_SOURCE_Y_INTEGER				27 : 16
#define	CSC_Y_SOURCE_Y_FRACTION				15 : 3

#define	CSC_U_SOURCE_BASE				0x0000D8
#define	CSC_U_SOURCE_BASE_EXT				27 : 27
#define	CSC_U_SOURCE_BASE_EXT_LOCAL			0
#define	CSC_U_SOURCE_BASE_EXT_EXTERNAL			1
#define	CSC_U_SOURCE_BASE_CS				26 : 26
#define	CSC_U_SOURCE_BASE_CS_0				0
#define	CSC_U_SOURCE_BASE_CS_1				1
#define	CSC_U_SOURCE_BASE_ADDRESS			25 : 0

#define	CSC_V_SOURCE_BASE				0x0000DC
#define	CSC_V_SOURCE_BASE_EXT				27 : 27
#define	CSC_V_SOURCE_BASE_EXT_LOCAL			0
#define	CSC_V_SOURCE_BASE_EXT_EXTERNAL			1
#define	CSC_V_SOURCE_BASE_CS				26 : 26
#define	CSC_V_SOURCE_BASE_CS_0				0
#define	CSC_V_SOURCE_BASE_CS_1				1
#define	CSC_V_SOURCE_BASE_ADDRESS			25 : 0

#define	CSC_SOURCE_DIMENSION				0x0000E0
#define	CSC_SOURCE_DIMENSION_X				31 : 16
#define	CSC_SOURCE_DIMENSION_Y				15 : 0

#define	CSC_SOURCE_PITCH				0x0000E4
#define	CSC_SOURCE_PITCH_Y				31 : 16
#define	CSC_SOURCE_PITCH_UV				15 : 0

#define	CSC_DESTINATION					0x0000E8
#define	CSC_DESTINATION_WRAP				31 : 31
#define	CSC_DESTINATION_WRAP_DISABLE			0
#define	CSC_DESTINATION_WRAP_ENABLE			1
#define	CSC_DESTINATION_X				27 : 16
#define	CSC_DESTINATION_Y				11 : 0

#define	CSC_DESTINATION_DIMENSION			0x0000EC
#define	CSC_DESTINATION_DIMENSION_X			31 : 16
#define	CSC_DESTINATION_DIMENSION_Y			15 : 0

#define	CSC_DESTINATION_PITCH				0x0000F0
#define	CSC_DESTINATION_PITCH_X				31 : 16
#define	CSC_DESTINATION_PITCH_Y				15 : 0

#define	CSC_SCALE_FACTOR				0x0000F4
#define	CSC_SCALE_FACTOR_HORIZONTAL			31 : 16
#define	CSC_SCALE_FACTOR_VERTICAL			15 : 0

#define	CSC_DESTINATION_BASE				0x0000F8
#define	CSC_DESTINATION_BASE_EXT			27 : 27
#define	CSC_DESTINATION_BASE_EXT_LOCAL			0
#define	CSC_DESTINATION_BASE_EXT_EXTERNAL		1
#define	CSC_DESTINATION_BASE_CS				26 : 26
#define	CSC_DESTINATION_BASE_CS_0			0
#define	CSC_DESTINATION_BASE_CS_1			1
#define	CSC_DESTINATION_BASE_ADDRESS			25 : 0

#define	CSC_CONTROL					0x0000FC
#define	CSC_CONTROL_STATUS				31 : 31
#define	CSC_CONTROL_STATUS_STOP				0
#define	CSC_CONTROL_STATUS_START			1
#define	CSC_CONTROL_SOURCE_FORMAT			30 : 28
#define	CSC_CONTROL_SOURCE_FORMAT_YUV422		0
#define	CSC_CONTROL_SOURCE_FORMAT_YUV420I		1
#define	CSC_CONTROL_SOURCE_FORMAT_YUV420		2
#define	CSC_CONTROL_SOURCE_FORMAT_YVU9			3
#define	CSC_CONTROL_SOURCE_FORMAT_IYU1			4
#define	CSC_CONTROL_SOURCE_FORMAT_IYU2			5
#define	CSC_CONTROL_SOURCE_FORMAT_RGB565		6
#define	CSC_CONTROL_SOURCE_FORMAT_RGB8888		7
#define	CSC_CONTROL_DESTINATION_FORMAT			27 : 26
#define	CSC_CONTROL_DESTINATION_FORMAT_RGB565		0
#define	CSC_CONTROL_DESTINATION_FORMAT_RGB8888		1
#define	CSC_CONTROL_HORIZONTAL_FILTER			25 : 25
#define	CSC_CONTROL_HORIZONTAL_FILTER_DISABLE		0
#define	CSC_CONTROL_HORIZONTAL_FILTER_ENABLE		1
#define	CSC_CONTROL_VERTICAL_FILTER			24 : 24
#define	CSC_CONTROL_VERTICAL_FILTER_DISABLE		0
#define	CSC_CONTROL_VERTICAL_FILTER_ENABLE		1
#define	CSC_CONTROL_BYTE_ORDER				23 : 23
#define	CSC_CONTROL_BYTE_ORDER_YUYV			0
#define	CSC_CONTROL_BYTE_ORDER_UYVY			1

#define	DE_DATA_PORT_501				0x110000
#define	DE_DATA_PORT_712				0x400000
#define	DE_DATA_PORT_722				0x6000

/* point to virtual Memory Map IO starting address */
extern char *smtc_RegBaseAddress;
/* point to virtual video memory starting address */
extern char *smtc_VRAMBaseAddress;
extern unsigned char smtc_de_busy;

extern unsigned long memRead32(unsigned long nOffset);
extern void memWrite32(unsigned long nOffset, unsigned long nData);
extern unsigned long SMTC_read2Dreg(unsigned long nOffset);

/* 2D functions */
extern void deInit(unsigned int nModeWidth, unsigned int nModeHeight,
		   unsigned int bpp);

extern void deWaitForNotBusy(void);

extern void deVerticalLine(unsigned long dst_base,
	unsigned long dst_pitch,
	unsigned long nX,
	unsigned long nY,
	unsigned long dst_height,
	unsigned long nColor);

extern void deHorizontalLine(unsigned long dst_base,
	unsigned long dst_pitch,
	unsigned long nX,
	unsigned long nY,
	unsigned long dst_width,
	unsigned long nColor);

extern void deLine(unsigned long dst_base,
	unsigned long dst_pitch,
	unsigned long nX1,
	unsigned long nY1,
	unsigned long nX2,
	unsigned long nY2,
	unsigned long nColor);

extern void deFillRect(unsigned long dst_base,
	unsigned long dst_pitch,
	unsigned long dst_X,
	unsigned long dst_Y,
	unsigned long dst_width,
	unsigned long dst_height,
	unsigned long nColor);

extern void deRotatePattern(unsigned char *pattern_dstaddr,
	unsigned long pattern_src_addr,
	unsigned long pattern_BPP,
	unsigned long pattern_stride,
	int	patternX,
	int	patternY);

extern void deCopy(unsigned long dst_base,
	unsigned long dst_pitch,
	unsigned long dst_BPP,
	unsigned long dst_X,
	unsigned long dst_Y,
	unsigned long dst_width,
	unsigned long dst_height,
	unsigned long src_base,
	unsigned long src_pitch,
	unsigned long src_X,
	unsigned long src_Y,
	pTransparent	pTransp,
	unsigned char nROP2);

/*
 * System memory to Video memory monochrome expansion.
 *
 * Source is monochrome image in system memory.  This function expands the
 * monochrome data to color image in video memory.
 *
 * @pSrcbuf: pointer to start of source buffer in system memory
 * @srcDelta: Pitch value (in bytes) of the source buffer, +ive means top
 * 		down and -ive mean button up
 * @startBit: Mono data can start at any bit in a byte, this value should
 * 		be 0 to 7
 * @dBase: Address of destination :  offset in frame buffer
 * @dPitch: Pitch value of destination surface in BYTE
 * @bpp: Color depth of destination surface
 * @dx, dy: Starting coordinate of destination surface
 * @width, height: width and height of rectange in pixel value
 * @fColor,bColor: Foreground, Background color (corresponding to a 1, 0 in
 * 	the monochrome data)
 * @rop2: ROP value
 */

extern long deSystemMem2VideoMemMonoBlt(
	const char *pSrcbuf,
	long srcDelta,
	unsigned long startBit,
	unsigned long dBase,
	unsigned long dPitch,
	unsigned long bpp,
	unsigned long dx, unsigned long dy,
	unsigned long width, unsigned long height,
	unsigned long fColor,
	unsigned long bColor,
	unsigned long rop2);

extern unsigned long deGetTransparency(void);
extern void deSetPixelFormat(unsigned long bpp);
