#ifndef ACCEL_H__
#define ACCEL_H__

#define HW_ROP2_COPY 0xc
#define HW_ROP2_XOR 0x6

/* notes: below address are the offset value from de_base_address (0x100000)*/

/* for sm718/750/502 de_base is at mmreg_1mb*/
#define DE_BASE_ADDR_TYPE1 0x100000
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
#define DE_SOURCE_WRAP                                  BIT(31)
#define DE_SOURCE_X_K1_SHIFT                            16
#define DE_SOURCE_X_K1_MASK                             (0x3fff << 16)
#define DE_SOURCE_X_K1_MONO_MASK			(0x1f << 16)
#define DE_SOURCE_Y_K2_MASK                             0xffff

#define DE_DESTINATION                                  0x4
#define DE_DESTINATION_WRAP                             BIT(31)
#define DE_DESTINATION_X_SHIFT                          16
#define DE_DESTINATION_X_MASK                           (0x1fff << 16)
#define DE_DESTINATION_Y_MASK                           0xffff

#define DE_DIMENSION                                    0x8
#define DE_DIMENSION_X_SHIFT                            16
#define DE_DIMENSION_X_MASK                             (0x1fff << 16)
#define DE_DIMENSION_Y_ET_MASK                          0x1fff

#define DE_CONTROL                                      0xC
#define DE_CONTROL_STATUS                               BIT(31)
#define DE_CONTROL_PATTERN                              BIT(30)
#define DE_CONTROL_UPDATE_DESTINATION_X                 BIT(29)
#define DE_CONTROL_QUICK_START                          BIT(28)
#define DE_CONTROL_DIRECTION                            BIT(27)
#define DE_CONTROL_MAJOR                                BIT(26)
#define DE_CONTROL_STEP_X                               BIT(25)
#define DE_CONTROL_STEP_Y                               BIT(24)
#define DE_CONTROL_STRETCH                              BIT(23)
#define DE_CONTROL_HOST                                 BIT(22)
#define DE_CONTROL_LAST_PIXEL                           BIT(21)
#define DE_CONTROL_COMMAND_SHIFT                        16
#define DE_CONTROL_COMMAND_MASK                         (0x1f << 16)
#define DE_CONTROL_COMMAND_BITBLT                       (0x0 << 16)
#define DE_CONTROL_COMMAND_RECTANGLE_FILL               (0x1 << 16)
#define DE_CONTROL_COMMAND_DE_TILE                      (0x2 << 16)
#define DE_CONTROL_COMMAND_TRAPEZOID_FILL               (0x3 << 16)
#define DE_CONTROL_COMMAND_ALPHA_BLEND                  (0x4 << 16)
#define DE_CONTROL_COMMAND_RLE_STRIP                    (0x5 << 16)
#define DE_CONTROL_COMMAND_SHORT_STROKE                 (0x6 << 16)
#define DE_CONTROL_COMMAND_LINE_DRAW                    (0x7 << 16)
#define DE_CONTROL_COMMAND_HOST_WRITE                   (0x8 << 16)
#define DE_CONTROL_COMMAND_HOST_READ                    (0x9 << 16)
#define DE_CONTROL_COMMAND_HOST_WRITE_BOTTOM_UP         (0xa << 16)
#define DE_CONTROL_COMMAND_ROTATE                       (0xb << 16)
#define DE_CONTROL_COMMAND_FONT                         (0xc << 16)
#define DE_CONTROL_COMMAND_TEXTURE_LOAD                 (0xe << 16)
#define DE_CONTROL_ROP_SELECT                           BIT(15)
#define DE_CONTROL_ROP2_SOURCE                          BIT(14)
#define DE_CONTROL_MONO_DATA_SHIFT                      12
#define DE_CONTROL_MONO_DATA_MASK                       (0x3 << 12)
#define DE_CONTROL_MONO_DATA_NOT_PACKED                 (0x0 << 12)
#define DE_CONTROL_MONO_DATA_8_PACKED                   (0x1 << 12)
#define DE_CONTROL_MONO_DATA_16_PACKED                  (0x2 << 12)
#define DE_CONTROL_MONO_DATA_32_PACKED                  (0x3 << 12)
#define DE_CONTROL_REPEAT_ROTATE                        BIT(11)
#define DE_CONTROL_TRANSPARENCY_MATCH                   BIT(10)
#define DE_CONTROL_TRANSPARENCY_SELECT                  BIT(9)
#define DE_CONTROL_TRANSPARENCY                         BIT(8)
#define DE_CONTROL_ROP_MASK                             0xff

/* Pseudo fields. */

#define DE_CONTROL_SHORT_STROKE_DIR_MASK                (0xf << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_225                 (0x0 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_135                 (0x1 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_315                 (0x2 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_45                  (0x3 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_270                 (0x4 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_90                  (0x5 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_180                 (0x8 << 24)
#define DE_CONTROL_SHORT_STROKE_DIR_0                   (0xa << 24)
#define DE_CONTROL_ROTATION_MASK                        (0x3 << 24)
#define DE_CONTROL_ROTATION_0                           (0x0 << 24)
#define DE_CONTROL_ROTATION_270                         (0x1 << 24)
#define DE_CONTROL_ROTATION_90                          (0x2 << 24)
#define DE_CONTROL_ROTATION_180                         (0x3 << 24)

#define DE_PITCH                                        0x000010
#define DE_PITCH_DESTINATION_SHIFT                      16
#define DE_PITCH_DESTINATION_MASK                       (0x1fff << 16)
#define DE_PITCH_SOURCE_MASK                            0x1fff

#define DE_FOREGROUND                                   0x000014
#define DE_FOREGROUND_COLOR_MASK                        0xffffffff

#define DE_BACKGROUND                                   0x000018
#define DE_BACKGROUND_COLOR_MASK                        0xffffffff

#define DE_STRETCH_FORMAT                               0x00001C
#define DE_STRETCH_FORMAT_PATTERN_XY                    BIT(30)
#define DE_STRETCH_FORMAT_PATTERN_Y_SHIFT               27
#define DE_STRETCH_FORMAT_PATTERN_Y_MASK                (0x7 << 27)
#define DE_STRETCH_FORMAT_PATTERN_X_SHIFT               23
#define DE_STRETCH_FORMAT_PATTERN_X_MASK                (0x7 << 23)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_SHIFT            20
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_MASK             (0x3 << 20)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_8                (0x0 << 20)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_16               (0x1 << 20)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_32               (0x2 << 20)
#define DE_STRETCH_FORMAT_PIXEL_FORMAT_24               (0x3 << 20)
#define DE_STRETCH_FORMAT_ADDRESSING_SHIFT              16
#define DE_STRETCH_FORMAT_ADDRESSING_MASK               (0xf << 16)
#define DE_STRETCH_FORMAT_ADDRESSING_XY                 (0x0 << 16)
#define DE_STRETCH_FORMAT_ADDRESSING_LINEAR             (0xf << 16)
#define DE_STRETCH_FORMAT_SOURCE_HEIGHT_MASK            0xfff

#define DE_COLOR_COMPARE                                0x000020
#define DE_COLOR_COMPARE_COLOR_MASK                     0xffffff

#define DE_COLOR_COMPARE_MASK                           0x000024
#define DE_COLOR_COMPARE_MASK_MASK                      0xffffff

#define DE_MASKS                                        0x000028
#define DE_MASKS_BYTE_MASK                              (0xffff << 16)
#define DE_MASKS_BIT_MASK                               0xffff

#define DE_CLIP_TL                                      0x00002C
#define DE_CLIP_TL_TOP_MASK                             (0xffff << 16)
#define DE_CLIP_TL_STATUS                               BIT(13)
#define DE_CLIP_TL_INHIBIT                              BIT(12)
#define DE_CLIP_TL_LEFT_MASK                            0xfff

#define DE_CLIP_BR                                      0x000030
#define DE_CLIP_BR_BOTTOM_MASK                          (0xffff << 16)
#define DE_CLIP_BR_RIGHT_MASK                           0x1fff

#define DE_MONO_PATTERN_LOW                             0x000034
#define DE_MONO_PATTERN_LOW_PATTERN_MASK                0xffffffff

#define DE_MONO_PATTERN_HIGH                            0x000038
#define DE_MONO_PATTERN_HIGH_PATTERN_MASK               0xffffffff

#define DE_WINDOW_WIDTH                                 0x00003C
#define DE_WINDOW_WIDTH_DST_SHIFT                       16
#define DE_WINDOW_WIDTH_DST_MASK                        (0x1fff << 16)
#define DE_WINDOW_WIDTH_SRC_MASK                        0x1fff

#define DE_WINDOW_SOURCE_BASE                           0x000040
#define DE_WINDOW_SOURCE_BASE_EXT                       BIT(27)
#define DE_WINDOW_SOURCE_BASE_CS                        BIT(26)
#define DE_WINDOW_SOURCE_BASE_ADDRESS_MASK              0x3ffffff

#define DE_WINDOW_DESTINATION_BASE                      0x000044
#define DE_WINDOW_DESTINATION_BASE_EXT                  BIT(27)
#define DE_WINDOW_DESTINATION_BASE_CS                   BIT(26)
#define DE_WINDOW_DESTINATION_BASE_ADDRESS_MASK         0x3ffffff

#define DE_ALPHA                                        0x000048
#define DE_ALPHA_VALUE_MASK                             0xff

#define DE_WRAP                                         0x00004C
#define DE_WRAP_X_MASK                                  (0xffff << 16)
#define DE_WRAP_Y_MASK                                  0xffff

#define DE_STATUS                                       0x000050
#define DE_STATUS_CSC                                   BIT(1)
#define DE_STATUS_2D                                    BIT(0)

/* blt direction */
#define TOP_TO_BOTTOM 0
#define LEFT_TO_RIGHT 0
#define BOTTOM_TO_TOP 1
#define RIGHT_TO_LEFT 1

void sm750_hw_set2dformat(struct lynx_accel *accel, int fmt);

void sm750_hw_de_init(struct lynx_accel *accel);

int sm750_hw_fillrect(struct lynx_accel *accel,
				u32 base, u32 pitch, u32 Bpp,
				u32 x, u32 y, u32 width, u32 height,
				u32 color, u32 rop);

int sm750_hw_copyarea(
struct lynx_accel *accel,
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

int sm750_hw_imageblit(struct lynx_accel *accel,
		 const char *pSrcbuf, /* pointer to start of source buffer in system memory */
		 u32 srcDelta,          /* Pitch value (in bytes) of the source buffer, +ive means top down and -ive mean button up */
		 u32 startBit, /* Mono data can start at any bit in a byte, this value should be 0 to 7 */
		 u32 dBase,    /* Address of destination: offset in frame buffer */
		 u32 dPitch,   /* Pitch value of destination surface in BYTE */
		 u32 bytePerPixel,      /* Color depth of destination surface */
		 u32 dx,
		 u32 dy,       /* Starting coordinate of destination surface */
		 u32 width,
		 u32 height,   /* width and height of rectangle in pixel value */
		 u32 fColor,   /* Foreground color (corresponding to a 1 in the monochrome data */
		 u32 bColor,   /* Background color (corresponding to a 0 in the monochrome data */
		 u32 rop2);
#endif
