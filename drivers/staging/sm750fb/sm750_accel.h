#ifndef ACCEL_H__
#define ACCEL_H__

#define HW_ROP2_COPY 0xc
#define HW_ROP2_XOR 0x6

/* notes: below address are the offset value from de_base_address (0x100000)*/

/* for sm718/750/502 de_base is at mmreg_1mb*/
#define DE_BASE_ADDR_TYPE1 	0x100000
/* for sm712,de_base is at mmreg_32kb */
#define DE_BASE_ADDR_TYPE2  0x8000
/* for sm722,de_base is at mmreg_0 */
#define DE_BASE_ADDR_TYPE3 0

/* type1 data port address is at mmreg_0x110000*/
#define DE_PORT_ADDR_TYPE1 0x110000
/* for sm712,data port address is at mmreg_0 */
#define DE_PORT_ADDR_TYPE2 0x100000
/* for sm722,data port address is at mmreg_1mb */
#define DE_PORT_ADDR_TYPE3 0x100000

#define DE_SOURCE                                       0x0
#define DE_SOURCE_WRAP                                  31:31
#define DE_SOURCE_WRAP_DISABLE                          0
#define DE_SOURCE_WRAP_ENABLE                           1
#define DE_SOURCE_X_K1                                  29:16
#define DE_SOURCE_Y_K2                                  15:0
#define DE_SOURCE_X_K1_MONO 							20:16

#define DE_DESTINATION                                  0x4
#define DE_DESTINATION_WRAP                             31:31
#define DE_DESTINATION_WRAP_DISABLE                     0
#define DE_DESTINATION_WRAP_ENABLE                      1
#define DE_DESTINATION_X                                28:16
#define DE_DESTINATION_Y                                15:0

#define DE_DIMENSION                                    0x8
#define DE_DIMENSION_X                                  28:16
#define DE_DIMENSION_Y_ET                               15:0

#define DE_CONTROL                                      0xC
#define DE_CONTROL_STATUS                               31:31
#define DE_CONTROL_STATUS_STOP                          0
#define DE_CONTROL_STATUS_START                         1
#define DE_CONTROL_PATTERN                              30:30
#define DE_CONTROL_PATTERN_MONO                         0
#define DE_CONTROL_PATTERN_COLOR                        1
#define DE_CONTROL_UPDATE_DESTINATION_X                 29:29
#define DE_CONTROL_UPDATE_DESTINATION_X_DISABLE         0
#define DE_CONTROL_UPDATE_DESTINATION_X_ENABLE          1
#define DE_CONTROL_QUICK_START                          28:28
#define DE_CONTROL_QUICK_START_DISABLE                  0
#define DE_CONTROL_QUICK_START_ENABLE                   1
#define DE_CONTROL_DIRECTION                            27:27
#define DE_CONTROL_DIRECTION_LEFT_TO_RIGHT              0
#define DE_CONTROL_DIRECTION_RIGHT_TO_LEFT              1
#define DE_CONTROL_MAJOR                                26:26
#define DE_CONTROL_MAJOR_X                              0
#define DE_CONTROL_MAJOR_Y                              1
#define DE_CONTROL_STEP_X                               25:25
#define DE_CONTROL_STEP_X_POSITIVE                      1
#define DE_CONTROL_STEP_X_NEGATIVE                      0
#define DE_CONTROL_STEP_Y                               24:24
#define DE_CONTROL_STEP_Y_POSITIVE                      1
#define DE_CONTROL_STEP_Y_NEGATIVE                      0
#define DE_CONTROL_STRETCH                              23:23
#define DE_CONTROL_STRETCH_DISABLE                      0
#define DE_CONTROL_STRETCH_ENABLE                       1
#define DE_CONTROL_HOST                                 22:22
#define DE_CONTROL_HOST_COLOR                           0
#define DE_CONTROL_HOST_MONO                            1
#define DE_CONTROL_LAST_PIXEL                           21:21
#define DE_CONTROL_LAST_PIXEL_OFF                       0
#define DE_CONTROL_LAST_PIXEL_ON                        1
#define DE_CONTROL_COMMAND                              20:16
#define DE_CONTROL_COMMAND_BITBLT                       0
#define DE_CONTROL_COMMAND_RECTANGLE_FILL               1
#define DE_CONTROL_COMMAND_DE_TILE                      2
#define DE_CONTROL_COMMAND_TRAPEZOID_FILL               3
#define DE_CONTROL_COMMAND_ALPHA_BLEND                  4
#define DE_CONTROL_COMMAND_RLE_STRIP                    5
#define DE_CONTROL_COMMAND_SHORT_STROKE                 6
#define DE_CONTROL_COMMAND_LINE_DRAW                    7
#define DE_CONTROL_COMMAND_HOST_WRITE                   8
#define DE_CONTROL_COMMAND_HOST_READ                    9
#define DE_CONTROL_COMMAND_HOST_WRITE_BOTTOM_UP         10
#define DE_CONTROL_COMMAND_ROTATE                       11
#define DE_CONTROL_COMMAND_FONT                         12
#define DE_CONTROL_COMMAND_TEXTURE_LOAD                 15
#define DE_CONTROL_ROP_SELECT                           15:15
#define DE_CONTROL_ROP_SELECT_ROP3                      0
#define DE_CONTROL_ROP_SELECT_ROP2                      1
#define DE_CONTROL_ROP2_SOURCE                          14:14
#define DE_CONTROL_ROP2_SOURCE_BITMAP                   0
#define DE_CONTROL_ROP2_SOURCE_PATTERN                  1
#define DE_CONTROL_MONO_DATA                            13:12
#define DE_CONTROL_MONO_DATA_NOT_PACKED                 0
#define DE_CONTROL_MONO_DATA_8_PACKED                   1
#define DE_CONTROL_MONO_DATA_16_PACKED                  2
#define DE_CONTROL_MONO_DATA_32_PACKED                  3
#define DE_CONTROL_REPEAT_ROTATE                        11:11
#define DE_CONTROL_REPEAT_ROTATE_DISABLE                0
#define DE_CONTROL_REPEAT_ROTATE_ENABLE                 1
#define DE_CONTROL_TRANSPARENCY_MATCH                   10:10
#define DE_CONTROL_TRANSPARENCY_MATCH_OPAQUE            0
#define DE_CONTROL_TRANSPARENCY_MATCH_TRANSPARENT       1
#define DE_CONTROL_TRANSPARENCY_SELECT                  9:9
#define DE_CONTROL_TRANSPARENCY_SELECT_SOURCE           0
#define DE_CONTROL_TRANSPARENCY_SELECT_DESTINATION      1
#define DE_CONTROL_TRANSPARENCY                         8:8
#define DE_CONTROL_TRANSPARENCY_DISABLE                 0
#define DE_CONTROL_TRANSPARENCY_ENABLE                  1
#define DE_CONTROL_ROP                                  7:0

// Pseudo fields.

#define DE_CONTROL_SHORT_STROKE_DIR                     27:24
#define DE_CONTROL_SHORT_STROKE_DIR_225                 0
#define DE_CONTROL_SHORT_STROKE_DIR_135                 1
#define DE_CONTROL_SHORT_STROKE_DIR_315                 2
#define DE_CONTROL_SHORT_STROKE_DIR_45                  3
#define DE_CONTROL_SHORT_STROKE_DIR_270                 4
#define DE_CONTROL_SHORT_STROKE_DIR_90                  5
#define DE_CONTROL_SHORT_STROKE_DIR_180                 8
#define DE_CONTROL_SHORT_STROKE_DIR_0                   10
#define DE_CONTROL_ROTATION                             25:24
#define DE_CONTROL_ROTATION_0                           0
#define DE_CONTROL_ROTATION_270                         1
#define DE_CONTROL_ROTATION_90                          2
#define DE_CONTROL_ROTATION_180                         3

#define DE_PITCH                                        0x000010
#define DE_PITCH_DESTINATION                            28:16
#define DE_PITCH_SOURCE                                 12:0

#define DE_FOREGROUND                                   0x000014
#define DE_FOREGROUND_COLOR                             31:0

#define DE_BACKGROUND                                   0x000018
#define DE_BACKGROUND_COLOR                             31:0

#define DE_STRETCH_FORMAT                               0x00001C
#define DE_STRETCH_FORMAT_PATTERN_XY                    30:30
#define DE_STRETCH_FORMAT_PATTERN_XY_NORMAL             0
#define DE_STRETCH_FORMAT_PATTERN_XY_OVERWRITE          1
#define DE_STRETCH_FORMAT_PATTERN_Y                     29:27
#define DE_STRETCH_FORMAT_PATTERN_X                     25:23
#define DE_STRETCH_FORMAT_PIXEL_FORMAT                  21:20
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_8                0
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_16               1
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_32               2
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_24               3

#define DE_STRETCH_FORMAT_ADDRESSING                    19:16
#define DE_STRETCH_FORMAT_ADDRESSING_XY                 0
#define DE_STRETCH_FORMAT_ADDRESSING_LINEAR             15
#define DE_STRETCH_FORMAT_SOURCE_HEIGHT                 11:0

#define DE_COLOR_COMPARE                                0x000020
#define DE_COLOR_COMPARE_COLOR                          23:0

#define DE_COLOR_COMPARE_MASK                           0x000024
#define DE_COLOR_COMPARE_MASK_MASKS                     23:0

#define DE_MASKS                                        0x000028
#define DE_MASKS_BYTE_MASK                              31:16
#define DE_MASKS_BIT_MASK                               15:0

#define DE_CLIP_TL                                      0x00002C
#define DE_CLIP_TL_TOP                                  31:16
#define DE_CLIP_TL_STATUS                               13:13
#define DE_CLIP_TL_STATUS_DISABLE                       0
#define DE_CLIP_TL_STATUS_ENABLE                        1
#define DE_CLIP_TL_INHIBIT                              12:12
#define DE_CLIP_TL_INHIBIT_OUTSIDE                      0
#define DE_CLIP_TL_INHIBIT_INSIDE                       1
#define DE_CLIP_TL_LEFT                                 11:0

#define DE_CLIP_BR                                      0x000030
#define DE_CLIP_BR_BOTTOM                               31:16
#define DE_CLIP_BR_RIGHT                                12:0

#define DE_MONO_PATTERN_LOW                             0x000034
#define DE_MONO_PATTERN_LOW_PATTERN                     31:0

#define DE_MONO_PATTERN_HIGH                            0x000038
#define DE_MONO_PATTERN_HIGH_PATTERN                    31:0

#define DE_WINDOW_WIDTH                                 0x00003C
#define DE_WINDOW_WIDTH_DESTINATION                     28:16
#define DE_WINDOW_WIDTH_SOURCE                          12:0

#define DE_WINDOW_SOURCE_BASE                           0x000040
#define DE_WINDOW_SOURCE_BASE_EXT                       27:27
#define DE_WINDOW_SOURCE_BASE_EXT_LOCAL                 0
#define DE_WINDOW_SOURCE_BASE_EXT_EXTERNAL              1
#define DE_WINDOW_SOURCE_BASE_CS                        26:26
#define DE_WINDOW_SOURCE_BASE_CS_0                      0
#define DE_WINDOW_SOURCE_BASE_CS_1                      1
#define DE_WINDOW_SOURCE_BASE_ADDRESS                   25:0

#define DE_WINDOW_DESTINATION_BASE                      0x000044
#define DE_WINDOW_DESTINATION_BASE_EXT                  27:27
#define DE_WINDOW_DESTINATION_BASE_EXT_LOCAL            0
#define DE_WINDOW_DESTINATION_BASE_EXT_EXTERNAL         1
#define DE_WINDOW_DESTINATION_BASE_CS                   26:26
#define DE_WINDOW_DESTINATION_BASE_CS_0                 0
#define DE_WINDOW_DESTINATION_BASE_CS_1                 1
#define DE_WINDOW_DESTINATION_BASE_ADDRESS              25:0

#define DE_ALPHA                                        0x000048
#define DE_ALPHA_VALUE                                  7:0

#define DE_WRAP                                         0x00004C
#define DE_WRAP_X                                       31:16
#define DE_WRAP_Y                                       15:0

#define DE_STATUS                                       0x000050
#define DE_STATUS_CSC                                   1:1
#define DE_STATUS_CSC_CLEAR                             0
#define DE_STATUS_CSC_NOT_ACTIVE                        0
#define DE_STATUS_CSC_ACTIVE                            1
#define DE_STATUS_2D                                    0:0
#define DE_STATUS_2D_CLEAR                              0
#define DE_STATUS_2D_NOT_ACTIVE                         0
#define DE_STATUS_2D_ACTIVE                             1



/* blt direction */
#define TOP_TO_BOTTOM 0
#define LEFT_TO_RIGHT 0
#define BOTTOM_TO_TOP 1
#define RIGHT_TO_LEFT 1

void hw_set2dformat(struct lynx_accel * accel,int fmt);

void hw_de_init(struct lynx_accel * accel);

int hw_fillrect(struct lynx_accel * accel,
				u32 base,u32 pitch,u32 Bpp,
				u32 x,u32 y,u32 width,u32 height,
				u32 color,u32 rop);

int hw_copyarea(
struct lynx_accel * accel,
unsigned int sBase,  /* Address of source: offset in frame buffer */
unsigned int sPitch, /* Pitch value of source surface in BYTE */
unsigned int sx,
unsigned int sy,     /* Starting coordinate of source surface */
unsigned int dBase,  /* Address of destination: offset in frame buffer */
unsigned int dPitch, /* Pitch value of destination surface in BYTE */
unsigned int bpp,    /* Color depth of destination surface */
unsigned int dx,
unsigned int dy,     /* Starting coordinate of destination surface */
unsigned int width,
unsigned int height, /* width and height of rectangle in pixel value */
unsigned int rop2);

int hw_imageblit(struct lynx_accel *accel,
		 const char *pSrcbuf, /* pointer to start of source buffer in system memory */
		 u32 srcDelta,          /* Pitch value (in bytes) of the source buffer, +ive means top down and -ive mean button up */
		 u32 startBit, /* Mono data can start at any bit in a byte, this value should be 0 to 7 */
		 u32 dBase,    /* Address of destination: offset in frame buffer */
		 u32 dPitch,   /* Pitch value of destination surface in BYTE */
		 u32 bytePerPixel,      /* Color depth of destination surface */
		 u32 dx,
		 u32 dy,       /* Starting coordinate of destination surface */
		 u32 width,
		 u32 height,   /* width and height of rectange in pixel value */
		 u32 fColor,   /* Foreground color (corresponding to a 1 in the monochrome data */
		 u32 bColor,   /* Background color (corresponding to a 0 in the monochrome data */
		 u32 rop2);
#endif
