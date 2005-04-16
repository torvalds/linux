/*
 * linux/drivers/video/virgefb.h -- CyberVision64 definitions for the
 *                                  text console driver.
 *
 *   Copyright (c) 1998 Alan Bair
 *
 * This file is based on the initial port to Linux of grf_cvreg.h:
 *
 *   Copyright (c) 1997 Antonio Santos
 *
 * The original work is from the NetBSD CyberVision 64 framebuffer driver 
 * and support files (grf_cv.c, grf_cvreg.h, ite_cv.c):
 * Permission to use the source of this driver was obtained from the
 * author Michael Teske by Alan Bair.
 *
 *   Copyright (c) 1995 Michael Teske
 *
 * History:
 *
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/* Enhanced register mapping (MMIO mode) */

#define S3_CRTC_ADR    0x03d4
#define S3_CRTC_DATA   0x03d5

#define S3_REG_LOCK2	0x39
#define S3_HGC_MODE	0x45

#define S3_HWGC_ORGX_H	0x46
#define S3_HWGC_ORGX_L	0x47
#define S3_HWGC_ORGY_H	0x48
#define S3_HWGC_ORGY_L	0x49
#define S3_HWGC_DX	0x4e
#define S3_HWGC_DY	0x4f

#define S3_LAW_CTL	0x58

/**************************************************/

/*
 * Defines for the used register addresses (mw)
 *
 * NOTE: There are some registers that have different addresses when
 *       in mono or color mode. We only support color mode, and thus
 *       some addresses won't work in mono-mode!
 *
 * General and VGA-registers taken from retina driver. Fixed a few
 * bugs in it. (SR and GR read address is Port + 1, NOT Port)
 *
 */

/* General Registers: */
#define GREG_MISC_OUTPUT_R	0x03CC
#define GREG_MISC_OUTPUT_W	0x03C2	
#define GREG_FEATURE_CONTROL_R	0x03CA 
#define GREG_FEATURE_CONTROL_W	0x03DA
#define GREG_INPUT_STATUS0_R	0x03C2
#define GREG_INPUT_STATUS1_R	0x03DA

/* Setup Registers: */
#define SREG_VIDEO_SUBS_ENABLE	0x03C3	/* virge */

/* Attribute Controller: */
#define ACT_ADDRESS		0x03C0
#define ACT_ADDRESS_R		0x03C1
#define ACT_ADDRESS_W		0x03C0
#define ACT_ADDRESS_RESET	0x03DA
#define ACT_ID_PALETTE0		0x00
#define ACT_ID_PALETTE1		0x01
#define ACT_ID_PALETTE2		0x02
#define ACT_ID_PALETTE3		0x03
#define ACT_ID_PALETTE4		0x04
#define ACT_ID_PALETTE5		0x05
#define ACT_ID_PALETTE6		0x06
#define ACT_ID_PALETTE7		0x07
#define ACT_ID_PALETTE8		0x08
#define ACT_ID_PALETTE9		0x09
#define ACT_ID_PALETTE10	0x0A
#define ACT_ID_PALETTE11	0x0B
#define ACT_ID_PALETTE12	0x0C
#define ACT_ID_PALETTE13	0x0D
#define ACT_ID_PALETTE14	0x0E
#define ACT_ID_PALETTE15	0x0F
#define ACT_ID_ATTR_MODE_CNTL	0x10
#define ACT_ID_OVERSCAN_COLOR	0x11
#define ACT_ID_COLOR_PLANE_ENA	0x12
#define ACT_ID_HOR_PEL_PANNING	0x13
#define ACT_ID_COLOR_SELECT	0x14    /* virge PX_PADD  pixel padding register */

/* Graphics Controller: */
#define GCT_ADDRESS		0x03CE
#define GCT_ADDRESS_R		0x03CF
#define GCT_ADDRESS_W		0x03CF
#define GCT_ID_SET_RESET	0x00
#define GCT_ID_ENABLE_SET_RESET	0x01
#define GCT_ID_COLOR_COMPARE	0x02
#define GCT_ID_DATA_ROTATE	0x03
#define GCT_ID_READ_MAP_SELECT	0x04
#define GCT_ID_GRAPHICS_MODE	0x05
#define GCT_ID_MISC		0x06
#define GCT_ID_COLOR_XCARE	0x07
#define GCT_ID_BITMASK		0x08

/* Sequencer: */
#define SEQ_ADDRESS		0x03C4
#define SEQ_ADDRESS_R		0x03C5
#define SEQ_ADDRESS_W		0x03C5
#define SEQ_ID_RESET		0x00
#define SEQ_ID_CLOCKING_MODE	0x01
#define SEQ_ID_MAP_MASK		0x02
#define SEQ_ID_CHAR_MAP_SELECT	0x03
#define SEQ_ID_MEMORY_MODE	0x04
#define SEQ_ID_UNKNOWN1		0x05
#define SEQ_ID_UNKNOWN2		0x06
#define SEQ_ID_UNKNOWN3		0x07
/* S3 extensions */
#define SEQ_ID_UNLOCK_EXT	0x08
#define SEQ_ID_EXT_SEQ_REG9	0x09	/* b7 = 1 extended reg access by MMIO only */
#define SEQ_ID_BUS_REQ_CNTL	0x0A
#define SEQ_ID_EXT_MISC_SEQ	0x0B
#define SEQ_ID_UNKNOWN4		0x0C
#define SEQ_ID_EXT_SEQ		0x0D
#define SEQ_ID_UNKNOWN5		0x0E
#define SEQ_ID_UNKNOWN6		0x0F
#define SEQ_ID_MCLK_LO		0x10
#define SEQ_ID_MCLK_HI		0x11
#define SEQ_ID_DCLK_LO		0x12
#define SEQ_ID_DCLK_HI		0x13
#define SEQ_ID_CLKSYN_CNTL_1	0x14
#define SEQ_ID_CLKSYN_CNTL_2	0x15
#define SEQ_ID_CLKSYN_TEST_HI	0x16	/* reserved for S3 testing of the */
#define SEQ_ID_CLKSYN_TEST_LO	0x17	/* internal clock synthesizer   */
#define SEQ_ID_RAMDAC_CNTL	0x18
#define SEQ_ID_MORE_MAGIC	0x1A
#define SEQ_ID_SIGNAL_SELECT	0x1C	/* new for virge */

/* CRT Controller: */
#define CRT_ADDRESS		0x03D4
#define CRT_ADDRESS_R		0x03D5
#define CRT_ADDRESS_W		0x03D5
#define CRT_ID_HOR_TOTAL	0x00
#define CRT_ID_HOR_DISP_ENA_END	0x01
#define CRT_ID_START_HOR_BLANK	0x02
#define CRT_ID_END_HOR_BLANK	0x03
#define CRT_ID_START_HOR_RETR	0x04
#define CRT_ID_END_HOR_RETR	0x05
#define CRT_ID_VER_TOTAL	0x06
#define CRT_ID_OVERFLOW		0x07
#define CRT_ID_PRESET_ROW_SCAN	0x08
#define CRT_ID_MAX_SCAN_LINE	0x09
#define CRT_ID_CURSOR_START	0x0A
#define CRT_ID_CURSOR_END	0x0B
#define CRT_ID_START_ADDR_HIGH	0x0C
#define CRT_ID_START_ADDR_LOW	0x0D
#define CRT_ID_CURSOR_LOC_HIGH	0x0E
#define CRT_ID_CURSOR_LOC_LOW	0x0F
#define CRT_ID_START_VER_RETR	0x10
#define CRT_ID_END_VER_RETR	0x11
#define CRT_ID_VER_DISP_ENA_END	0x12
#define CRT_ID_SCREEN_OFFSET	0x13
#define CRT_ID_UNDERLINE_LOC	0x14
#define CRT_ID_START_VER_BLANK	0x15
#define CRT_ID_END_VER_BLANK	0x16
#define CRT_ID_MODE_CONTROL	0x17
#define CRT_ID_LINE_COMPARE	0x18
#define CRT_ID_GD_LATCH_RBACK	0x22
#define CRT_ID_ACT_TOGGLE_RBACK	0x24
#define CRT_ID_ACT_INDEX_RBACK	0x26
/* S3 extensions: S3 VGA Registers */
#define CRT_ID_DEVICE_HIGH	0x2D
#define CRT_ID_DEVICE_LOW	0x2E
#define CRT_ID_REVISION 	0x2F
#define CRT_ID_CHIP_ID_REV	0x30
#define CRT_ID_MEMORY_CONF	0x31
#define CRT_ID_BACKWAD_COMP_1	0x32
#define CRT_ID_BACKWAD_COMP_2	0x33
#define CRT_ID_BACKWAD_COMP_3	0x34
#define CRT_ID_REGISTER_LOCK	0x35
#define CRT_ID_CONFIG_1 	0x36
#define CRT_ID_CONFIG_2 	0x37
#define CRT_ID_REGISTER_LOCK_1	0x38
#define CRT_ID_REGISTER_LOCK_2	0x39
#define CRT_ID_MISC_1		0x3A
#define CRT_ID_DISPLAY_FIFO	0x3B
#define CRT_ID_LACE_RETR_START	0x3C
/* S3 extensions: System Control Registers  */
#define CRT_ID_SYSTEM_CONFIG	0x40
#define CRT_ID_BIOS_FLAG	0x41
#define CRT_ID_LACE_CONTROL	0x42
#define CRT_ID_EXT_MODE 	0x43
#define CRT_ID_HWGC_MODE	0x45	/* HWGC = Hardware Graphics Cursor */
#define CRT_ID_HWGC_ORIGIN_X_HI	0x46
#define CRT_ID_HWGC_ORIGIN_X_LO	0x47
#define CRT_ID_HWGC_ORIGIN_Y_HI	0x48
#define CRT_ID_HWGC_ORIGIN_Y_LO	0x49
#define CRT_ID_HWGC_FG_STACK	0x4A
#define CRT_ID_HWGC_BG_STACK	0x4B
#define CRT_ID_HWGC_START_AD_HI	0x4C
#define CRT_ID_HWGC_START_AD_LO	0x4D
#define CRT_ID_HWGC_DSTART_X	0x4E
#define CRT_ID_HWGC_DSTART_Y	0x4F
/* S3 extensions: System Extension Registers  */
#define CRT_ID_EXT_SYS_CNTL_1	0x50	/* NOT a virge register */
#define CRT_ID_EXT_SYS_CNTL_2	0x51
#define CRT_ID_EXT_BIOS_FLAG_1	0x52
#define CRT_ID_EXT_MEM_CNTL_1	0x53
#define CRT_ID_EXT_MEM_CNTL_2	0x54
#define CRT_ID_EXT_DAC_CNTL	0x55
#define CRT_ID_EX_SYNC_1	0x56
#define CRT_ID_EX_SYNC_2	0x57
#define CRT_ID_LAW_CNTL		0x58	/* LAW = Linear Address Window */
#define CRT_ID_LAW_POS_HI	0x59
#define CRT_ID_LAW_POS_LO	0x5A
#define CRT_ID_GOUT_PORT	0x5C
#define CRT_ID_EXT_HOR_OVF	0x5D
#define CRT_ID_EXT_VER_OVF	0x5E
#define CRT_ID_EXT_MEM_CNTL_3	0x60	/* NOT a virge register */
#define CRT_ID_EXT_MEM_CNTL_4	0x61
#define CRT_ID_EX_SYNC_3	0x63	/* NOT a virge register */
#define CRT_ID_EXT_MISC_CNTL	0x65
#define CRT_ID_EXT_MISC_CNTL_1	0x66
#define CRT_ID_EXT_MISC_CNTL_2	0x67
#define CRT_ID_CONFIG_3 	0x68
#define CRT_ID_EXT_SYS_CNTL_3	0x69
#define CRT_ID_EXT_SYS_CNTL_4	0x6A
#define CRT_ID_EXT_BIOS_FLAG_3	0x6B
#define CRT_ID_EXT_BIOS_FLAG_4	0x6C
/* S3 virge extensions: more System Extension Registers  */
#define CRT_ID_EXT_BIOS_FLAG_5	0x6D
#define CRT_ID_EXT_DAC_TEST	0x6E
#define CRT_ID_CONFIG_4 	0x6F

/* Video DAC */
#define VDAC_ADDRESS		0x03c8
#define VDAC_ADDRESS_W		0x03c8
#define VDAC_ADDRESS_R		0x03c7
#define VDAC_STATE		0x03c7
#define VDAC_DATA		0x03c9
#define VDAC_MASK		0x03c6

/* Miscellaneous Registers */
#define MR_SUBSYSTEM_STATUS_R		0x8504	/* new for virge */
#define MR_SUBSYSTEM_CNTL_W		0x8504	/* new for virge */
#define MR_ADVANCED_FUNCTION_CONTROL	0x850C	/* new for virge */

/* Blitter  */
#define BLT_COMMAND_SET		0xA500
#define BLT_SIZE_X_Y		0xA504
#define BLT_SRC_X_Y		0xA508
#define BLT_DEST_X_Y		0xA50C

#define BLT_SRC_BASE		0xa4d4
#define BLT_DEST_BASE		0xa4d8
#define BLT_CLIP_LEFT_RIGHT	0xa4dc
#define BLT_CLIP_TOP_BOTTOM	0xa4e0
#define BLT_SRC_DEST_STRIDE	0xa4e4
#define BLT_MONO_PATTERN_0	0xa4e8
#define BLT_MONO_PATTERN_1	0xa4ec
#define BLT_PATTERN_COLOR	0xa4f4

#define L2D_COMMAND_SET		0xA900
#define L2D_CLIP_LEFT_RIGHT	0xA8DC
#define L2D_CLIP_TOP_BOTTOM	0xA8E0

#define P2D_COMMAND_SET		0xAD00
#define P2D_CLIP_LEFT_RIGHT	0xACDC
#define P2D_CLIP_TOP_BOTTOM	0xACE0

#define CMD_NOP		(0xf << 27)	/* %1111 << 27, was 0x07 */ 
#define S3V_BITBLT	(0x0 << 27)
#define S3V_RECTFILL	(0x2 << 27)
#define S3V_AUTOEXE	0x01
#define S3V_HWCLIP	0x02
#define S3V_DRAW	0x20
#define S3V_DST_8BPP	0x00
#define S3V_DST_16BPP	0x04
#define S3V_DST_24BPP	0x08
#define S3V_MONO_PAT	0x100

#define S3V_BLT_COPY	(0xcc<<17)
#define S3V_BLT_CLEAR	(0x00<<17)
#define S3V_BLT_SET	(0xff<<17)
