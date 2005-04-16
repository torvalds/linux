/*
 * linux/drivers/video/retz3fb.h -- Defines and macros for the RetinaZ3 frame
 *				    buffer device
 *
 *    Copyright (C) 1997 Jes Sorensen
 *
 * History:
 *   - 22 Jan 97: Initial work
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Macros to read and write to registers.
 */
#define reg_w(regs, reg,dat) (*(regs + reg) = dat)
#define reg_r(regs, reg) (*(regs + reg))

/*
 * Macro to access the sequencer.
 */
#define seq_w(regs, sreg, sdat) \
	do{ reg_w(regs, SEQ_IDX, sreg); reg_w(regs, SEQ_DATA, sdat); } while(0)

/*
 * Macro to access the CRT controller.
 */
#define crt_w(regs, creg, cdat) \
	do{ reg_w(regs, CRT_IDX, creg); reg_w(regs, CRT_DATA, cdat); } while(0)

/*
 * Macro to access the graphics controller.
 */
#define gfx_w(regs, greg, gdat) \
	do{ reg_w(regs, GFX_IDX, greg); reg_w(regs, GFX_DATA, gdat); } while(0)

/*
 * Macro to access the attribute controller.
 */
#define attr_w(regs, areg, adat) \
	do{ reg_w(regs, ACT_IDX, areg); reg_w(regs, ACT_DATA, adat); } while(0)

/*
 * Macro to access the pll.
 */
#define pll_w(regs, preg, pdat) \
	do{ reg_w(regs, PLL_IDX, preg); \
	    reg_w(regs, PLL_DATA, (pdat & 0xff)); \
	    reg_w(regs, PLL_DATA, (pdat >> 8));\
	} while(0)

/*
 * Offsets
 */
#define VIDEO_MEM_OFFSET	0x00c00000
#define ACM_OFFSET		0x00b00000

/*
 * Accelerator Control Menu
 */
#define ACM_PRIMARY_OFFSET	0x00
#define ACM_SECONDARY_OFFSET	0x04
#define ACM_MODE_CONTROL	0x08
#define ACM_CURSOR_POSITION	0x0c
#define ACM_START_STATUS	0x30
#define ACM_CONTROL		0x34
#define ACM_RASTEROP_ROTATION	0x38
#define ACM_BITMAP_DIMENSION	0x3c
#define ACM_DESTINATION		0x40
#define ACM_SOURCE		0x44
#define ACM_PATTERN		0x48
#define ACM_FOREGROUND		0x4c
#define ACM_BACKGROUND		0x50

/*
 * Video DAC addresses
 */
#define VDAC_ADDRESS		0x03c8
#define VDAC_ADDRESS_W		0x03c8
#define VDAC_ADDRESS_R		0x03c7
#define VDAC_STATE		0x03c7
#define VDAC_DATA		0x03c9
#define VDAC_MASK		0x03c6

/*
 * Sequencer
 */
#define SEQ_IDX			0x03c4	/* Sequencer Index */
#define SEQ_DATA		0x03c5
#define SEQ_RESET		0x00
#define SEQ_CLOCKING_MODE	0x01
#define SEQ_MAP_MASK		0x02
#define SEQ_CHAR_MAP_SELECT	0x03
#define SEQ_MEMORY_MODE		0x04
#define SEQ_EXTENDED_ENABLE	0x05	/* NCR extensions */
#define SEQ_UNKNOWN1         	0x06
#define SEQ_UNKNOWN2         	0x07
#define SEQ_CHIP_ID		0x08
#define SEQ_UNKNOWN3         	0x09
#define SEQ_CURSOR_COLOR1	0x0a
#define SEQ_CURSOR_COLOR0	0x0b
#define SEQ_CURSOR_CONTROL	0x0c
#define SEQ_CURSOR_X_LOC_HI	0x0d
#define SEQ_CURSOR_X_LOC_LO	0x0e
#define SEQ_CURSOR_Y_LOC_HI	0x0f
#define SEQ_CURSOR_Y_LOC_LO	0x10
#define SEQ_CURSOR_X_INDEX	0x11
#define SEQ_CURSOR_Y_INDEX	0x12
#define SEQ_CURSOR_STORE_HI	0x13
#define SEQ_CURSOR_STORE_LO	0x14
#define SEQ_CURSOR_ST_OFF_HI	0x15
#define SEQ_CURSOR_ST_OFF_LO	0x16
#define SEQ_CURSOR_PIXELMASK	0x17
#define SEQ_PRIM_HOST_OFF_HI	0x18
#define SEQ_PRIM_HOST_OFF_LO	0x19
#define SEQ_LINEAR_0		0x1a
#define SEQ_LINEAR_1		0x1b
#define SEQ_SEC_HOST_OFF_HI	0x1c
#define SEQ_SEC_HOST_OFF_LO	0x1d
#define SEQ_EXTENDED_MEM_ENA	0x1e
#define SEQ_EXT_CLOCK_MODE	0x1f
#define SEQ_EXT_VIDEO_ADDR	0x20
#define SEQ_EXT_PIXEL_CNTL	0x21
#define SEQ_BUS_WIDTH_FEEDB	0x22
#define SEQ_PERF_SELECT		0x23
#define SEQ_COLOR_EXP_WFG	0x24
#define SEQ_COLOR_EXP_WBG	0x25
#define SEQ_EXT_RW_CONTROL	0x26
#define SEQ_MISC_FEATURE_SEL	0x27
#define SEQ_COLOR_KEY_CNTL	0x28
#define SEQ_COLOR_KEY_MATCH0	0x29
#define SEQ_COLOR_KEY_MATCH1 	0x2a
#define SEQ_COLOR_KEY_MATCH2 	0x2b
#define SEQ_UNKNOWN6         	0x2c
#define SEQ_CRC_CONTROL		0x2d
#define SEQ_CRC_DATA_LOW	0x2e
#define SEQ_CRC_DATA_HIGH	0x2f
#define SEQ_MEMORY_MAP_CNTL	0x30
#define SEQ_ACM_APERTURE_1	0x31
#define SEQ_ACM_APERTURE_2	0x32
#define SEQ_ACM_APERTURE_3	0x33
#define SEQ_BIOS_UTILITY_0	0x3e
#define SEQ_BIOS_UTILITY_1	0x3f

/*
 * Graphics Controller
 */
#define GFX_IDX			0x03ce
#define GFX_DATA		0x03cf
#define GFX_SET_RESET		0x00
#define GFX_ENABLE_SET_RESET	0x01
#define GFX_COLOR_COMPARE	0x02
#define GFX_DATA_ROTATE		0x03
#define GFX_READ_MAP_SELECT	0x04
#define GFX_GRAPHICS_MODE	0x05
#define GFX_MISC		0x06
#define GFX_COLOR_XCARE		0x07
#define GFX_BITMASK		0x08

/*
 * CRT Controller
 */
#define CRT_IDX			0x03d4
#define CRT_DATA		0x03d5
#define CRT_HOR_TOTAL		0x00
#define CRT_HOR_DISP_ENA_END	0x01
#define CRT_START_HOR_BLANK	0x02
#define CRT_END_HOR_BLANK	0x03
#define CRT_START_HOR_RETR	0x04
#define CRT_END_HOR_RETR	0x05
#define CRT_VER_TOTAL		0x06
#define CRT_OVERFLOW		0x07
#define CRT_PRESET_ROW_SCAN	0x08
#define CRT_MAX_SCAN_LINE	0x09
#define CRT_CURSOR_START	0x0a
#define CRT_CURSOR_END		0x0b
#define CRT_START_ADDR_HIGH	0x0c
#define CRT_START_ADDR_LOW	0x0d
#define CRT_CURSOR_LOC_HIGH	0x0e
#define CRT_CURSOR_LOC_LOW	0x0f
#define CRT_START_VER_RETR	0x10
#define CRT_END_VER_RETR	0x11
#define CRT_VER_DISP_ENA_END	0x12
#define CRT_OFFSET		0x13
#define CRT_UNDERLINE_LOC	0x14
#define CRT_START_VER_BLANK	0x15
#define CRT_END_VER_BLANK	0x16
#define CRT_MODE_CONTROL	0x17
#define CRT_LINE_COMPARE	0x18
#define CRT_UNKNOWN1         	0x19
#define CRT_UNKNOWN2         	0x1a
#define CRT_UNKNOWN3         	0x1b
#define CRT_UNKNOWN4         	0x1c
#define CRT_UNKNOWN5         	0x1d
#define CRT_UNKNOWN6         	0x1e
#define CRT_UNKNOWN7         	0x1f
#define CRT_UNKNOWN8         	0x20
#define CRT_UNKNOWN9		0x21
#define CRT_UNKNOWN10		0x22
#define CRT_UNKNOWN11      	0x23
#define CRT_UNKNOWN12      	0x24
#define CRT_UNKNOWN13      	0x25
#define CRT_UNKNOWN14      	0x26
#define CRT_UNKNOWN15      	0x27
#define CRT_UNKNOWN16      	0x28
#define CRT_UNKNOWN17      	0x29
#define CRT_UNKNOWN18      	0x2a
#define CRT_UNKNOWN19      	0x2b
#define CRT_UNKNOWN20      	0x2c
#define CRT_UNKNOWN21      	0x2d
#define CRT_UNKNOWN22      	0x2e
#define CRT_UNKNOWN23      	0x2f
#define CRT_EXT_HOR_TIMING1	0x30	/* NCR crt extensions */
#define CRT_EXT_START_ADDR	0x31
#define CRT_EXT_HOR_TIMING2	0x32
#define CRT_EXT_VER_TIMING	0x33
#define CRT_MONITOR_POWER	0x34

/*
 * General Registers
 */
#define GREG_STATUS0_R		0x03c2
#define GREG_STATUS1_R		0x03da
#define GREG_MISC_OUTPUT_R	0x03cc
#define GREG_MISC_OUTPUT_W	0x03c2	
#define GREG_FEATURE_CONTROL_R	0x03ca
#define GREG_FEATURE_CONTROL_W	0x03da
#define GREG_POS		0x0102

/*
 * Attribute Controller
 */
#define ACT_IDX			0x03C0
#define ACT_ADDRESS_R		0x03C0
#define ACT_DATA		0x03C0
#define ACT_ADDRESS_RESET	0x03DA
#define ACT_PALETTE0		0x00
#define ACT_PALETTE1		0x01
#define ACT_PALETTE2		0x02
#define ACT_PALETTE3		0x03
#define ACT_PALETTE4		0x04
#define ACT_PALETTE5		0x05
#define ACT_PALETTE6		0x06
#define ACT_PALETTE7		0x07
#define ACT_PALETTE8		0x08
#define ACT_PALETTE9		0x09
#define ACT_PALETTE10		0x0A
#define ACT_PALETTE11		0x0B
#define ACT_PALETTE12		0x0C
#define ACT_PALETTE13		0x0D
#define ACT_PALETTE14		0x0E
#define ACT_PALETTE15		0x0F
#define ACT_ATTR_MODE_CNTL	0x10
#define ACT_OVERSCAN_COLOR	0x11
#define ACT_COLOR_PLANE_ENA	0x12
#define ACT_HOR_PEL_PANNING	0x13
#define ACT_COLOR_SELECT	0x14

/*
 * PLL
 */
#define PLL_IDX			0x83c8
#define PLL_DATA		0x83c9

/*
 * Blitter operations
 */
#define	Z3BLTclear		0x00	/* 0 */
#define Z3BLTand		0x80	/* src AND dst */
#define Z3BLTandReverse		0x40	/* src AND NOT dst */
#define Z3BLTcopy		0xc0	/* src */
#define Z3BLTandInverted	0x20	/* NOT src AND dst */
#define	Z3BLTnoop		0xa0	/* dst */
#define Z3BLTxor		0x60	/* src XOR dst */
#define Z3BLTor			0xe0	/* src OR dst */
#define Z3BLTnor		0x10	/* NOT src AND NOT dst */
#define Z3BLTequiv		0x90	/* NOT src XOR dst */
#define Z3BLTinvert		0x50	/* NOT dst */
#define Z3BLTorReverse		0xd0	/* src OR NOT dst */
#define Z3BLTcopyInverted	0x30	/* NOT src */
#define Z3BLTorInverted		0xb0	/* NOT src OR dst */
#define Z3BLTnand		0x70	/* NOT src OR NOT dst */
#define Z3BLTset		0xf0	/* 1 */
