/*
 * linux/arch/m68k/console/cvision.h -- CyberVision64 definitions for the
 *                                      text console driver.
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

/* s3 commands */
#define S3_BITBLT       0xc011
#define S3_TWOPOINTLINE 0x2811
#define S3_FILLEDRECT   0x40b1

#define S3_FIFO_EMPTY 0x0400
#define S3_HDW_BUSY   0x0200

/* Enhanced register mapping (MMIO mode) */

#define S3_READ_SEL      0xbee8 /* offset f */
#define S3_MULT_MISC     0xbee8 /* offset e */
#define S3_ERR_TERM      0x92e8
#define S3_FRGD_COLOR    0xa6e8
#define S3_BKGD_COLOR    0xa2e8
#define S3_PIXEL_CNTL    0xbee8 /* offset a */
#define S3_FRGD_MIX      0xbae8
#define S3_BKGD_MIX      0xb6e8
#define S3_CUR_Y         0x82e8
#define S3_CUR_X         0x86e8
#define S3_DESTY_AXSTP   0x8ae8
#define S3_DESTX_DIASTP  0x8ee8
#define S3_MIN_AXIS_PCNT 0xbee8 /* offset 0 */
#define S3_MAJ_AXIS_PCNT 0x96e8
#define S3_CMD           0x9ae8
#define S3_GP_STAT       0x9ae8
#define S3_ADVFUNC_CNTL  0x4ae8
#define S3_WRT_MASK      0xaae8
#define S3_RD_MASK       0xaee8

/* Enhanced register mapping (Packed MMIO mode, write only) */
#define S3_ALT_CURXY     0x8100
#define S3_ALT_CURXY2    0x8104
#define S3_ALT_STEP      0x8108
#define S3_ALT_STEP2     0x810c
#define S3_ALT_ERR       0x8110
#define S3_ALT_CMD       0x8118
#define S3_ALT_MIX       0x8134
#define S3_ALT_PCNT      0x8148
#define S3_ALT_PAT       0x8168

/* Drawing modes */
#define S3_NOTCUR          0x0000
#define S3_LOGICALZERO     0x0001
#define S3_LOGICALONE      0x0002
#define S3_LEAVEASIS       0x0003
#define S3_NOTNEW          0x0004
#define S3_CURXORNEW       0x0005
#define S3_NOT_CURXORNEW   0x0006
#define S3_NEW             0x0007
#define S3_NOTCURORNOTNEW  0x0008
#define S3_CURORNOTNEW     0x0009
#define S3_NOTCURORNEW     0x000a
#define S3_CURORNEW        0x000b
#define S3_CURANDNEW       0x000c
#define S3_NOTCURANDNEW    0x000d
#define S3_CURANDNOTNEW    0x000e
#define S3_NOTCURANDNOTNEW 0x000f

#define S3_CRTC_ADR    0x03d4
#define S3_CRTC_DATA   0x03d5

#define S3_REG_LOCK2 0x39
#define S3_HGC_MODE  0x45

#define S3_HWGC_ORGX_H 0x46
#define S3_HWGC_ORGX_L 0x47
#define S3_HWGC_ORGY_H 0x48
#define S3_HWGC_ORGY_L 0x49
#define S3_HWGC_DX     0x4e
#define S3_HWGC_DY     0x4f

#define S3_LAW_CTL 0x58

/**************************************************/

/* support for a BitBlt operation. The op-codes are identical
   to X11 GCs */
#define	GRFBBOPclear		0x0	/* 0 */
#define GRFBBOPand		0x1	/* src AND dst */
#define GRFBBOPandReverse	0x2	/* src AND NOT dst */
#define GRFBBOPcopy		0x3	/* src */
#define GRFBBOPandInverted	0x4	/* NOT src AND dst */
#define	GRFBBOPnoop		0x5	/* dst */
#define GRFBBOPxor		0x6	/* src XOR dst */
#define GRFBBOPor		0x7	/* src OR dst */
#define GRFBBOPnor		0x8	/* NOT src AND NOT dst */
#define GRFBBOPequiv		0x9	/* NOT src XOR dst */
#define GRFBBOPinvert		0xa	/* NOT dst */
#define GRFBBOPorReverse	0xb	/* src OR NOT dst */
#define GRFBBOPcopyInverted	0xc	/* NOT src */
#define GRFBBOPorInverted	0xd	/* NOT src OR dst */
#define GRFBBOPnand		0xe	/* NOT src OR NOT dst */
#define GRFBBOPset		0xf	/* 1 */


/* Write 16 Bit VGA register */
#define vgaw16(ba, reg, val) \
*((unsigned short *)  (((volatile unsigned char *)ba)+reg)) = val

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
#define SREG_OPTION_SELECT	0x0102
#define SREG_VIDEO_SUBS_ENABLE	0x46E8

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
#define ACT_ID_COLOR_SELECT	0x14

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
#define SEQ_ID_EXT_SEQ_REG9	0x09
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
#define SEQ_ID_CLKSYN_TEST_LO	0x17	/*   internal clock synthesizer   */
#define SEQ_ID_RAMDAC_CNTL	0x18
#define SEQ_ID_MORE_MAGIC	0x1A

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
#define CRT_ID_EXT_SYS_CNTL_1	0x50
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
#define CRT_ID_EXT_MEM_CNTL_3	0x60
#define CRT_ID_EX_SYNC_3	0x63
#define CRT_ID_EXT_MISC_CNTL	0x65
#define CRT_ID_EXT_MISC_CNTL_1	0x66
#define CRT_ID_EXT_MISC_CNTL_2	0x67
#define CRT_ID_CONFIG_3 	0x68
#define CRT_ID_EXT_SYS_CNTL_3	0x69
#define CRT_ID_EXT_SYS_CNTL_4	0x6A
#define CRT_ID_EXT_BIOS_FLAG_3	0x6B
#define CRT_ID_EXT_BIOS_FLAG_4	0x6C

/* Enhanced Commands Registers: */
#define ECR_SUBSYSTEM_STAT	0x42E8
#define ECR_SUBSYSTEM_CNTL	0x42E8
#define ECR_ADV_FUNC_CNTL	0x4AE8
#define ECR_CURRENT_Y_POS	0x82E8
#define ECR_CURRENT_Y_POS2	0x82EA	/* Trio64 only */
#define ECR_CURRENT_X_POS	0x86E8
#define ECR_CURRENT_X_POS2	0x86EA	/* Trio64 only */
#define ECR_DEST_Y__AX_STEP	0x8AE8
#define ECR_DEST_Y2__AX_STEP2	0x8AEA	/* Trio64 only */
#define ECR_DEST_X__DIA_STEP	0x8EE8
#define ECR_DEST_X2__DIA_STEP2	0x8EEA	/* Trio64 only */
#define ECR_ERR_TERM		0x92E8
#define ECR_ERR_TERM2		0x92EA	/* Trio64 only */
#define ECR_MAJ_AXIS_PIX_CNT	0x96E8
#define ECR_MAJ_AXIS_PIX_CNT2	0x96EA	/* Trio64 only */
#define ECR_GP_STAT		0x9AE8	/* GP = Graphics Processor */
#define ECR_DRAW_CMD		0x9AE8
#define ECR_DRAW_CMD2		0x9AEA	/* Trio64 only */
#define ECR_SHORT_STROKE	0x9EE8
#define ECR_BKGD_COLOR		0xA2E8	/* BKGD = Background */
#define ECR_FRGD_COLOR		0xA6E8	/* FRGD = Foreground */
#define ECR_BITPLANE_WRITE_MASK	0xAAE8
#define ECR_BITPLANE_READ_MASK	0xAEE8
#define ECR_COLOR_COMPARE	0xB2E8
#define ECR_BKGD_MIX		0xB6E8
#define ECR_FRGD_MIX		0xBAE8
#define ECR_READ_REG_DATA	0xBEE8
#define ECR_ID_MIN_AXIS_PIX_CNT	0x00
#define ECR_ID_SCISSORS_TOP	0x01
#define ECR_ID_SCISSORS_LEFT	0x02
#define ECR_ID_SCISSORS_BUTTOM	0x03
#define ECR_ID_SCISSORS_RIGHT	0x04
#define ECR_ID_PIX_CNTL		0x0A
#define ECR_ID_MULT_CNTL_MISC_2	0x0D
#define ECR_ID_MULT_CNTL_MISC	0x0E
#define ECR_ID_READ_SEL		0x0F
#define ECR_PIX_TRANS		0xE2E8
#define ECR_PIX_TRANS_EXT	0xE2EA
#define ECR_PATTERN_Y		0xEAE8	/* Trio64 only */
#define ECR_PATTERN_X		0xEAEA	/* Trio64 only */


/* Pass-through */
#define PASS_ADDRESS		0x40001
#define PASS_ADDRESS_W		0x40001

/* Video DAC */
#define VDAC_ADDRESS		0x03c8
#define VDAC_ADDRESS_W		0x03c8
#define VDAC_ADDRESS_R		0x03c7
#define VDAC_STATE		0x03c7
#define VDAC_DATA		0x03c9
#define VDAC_MASK		0x03c6


#define WGfx(ba, idx, val) \
do { wb_64(ba, GCT_ADDRESS, idx); wb_64(ba, GCT_ADDRESS_W , val); } while (0)

#define WSeq(ba, idx, val) \
do { wb_64(ba, SEQ_ADDRESS, idx); wb_64(ba, SEQ_ADDRESS_W , val); } while (0)

#define WCrt(ba, idx, val) \
do { wb_64(ba, CRT_ADDRESS, idx); wb_64(ba, CRT_ADDRESS_W , val); } while (0)

#define WAttr(ba, idx, val) \
do { \
  unsigned char tmp;\
  tmp = rb_64(ba, ACT_ADDRESS_RESET);\
  wb_64(ba, ACT_ADDRESS_W, idx);\
  wb_64(ba, ACT_ADDRESS_W, val);\
} while (0)

#define SetTextPlane(ba, m) \
do { \
  WGfx(ba, GCT_ID_READ_MAP_SELECT, m & 3 );\
  WSeq(ba, SEQ_ID_MAP_MASK, (1 << (m & 3)));\
} while (0)

     /* --------------------------------- */
     /* prototypes                        */
     /* --------------------------------- */

inline unsigned char RAttr(volatile unsigned char * board, short idx);
inline unsigned char RSeq(volatile unsigned char * board, short idx);
inline unsigned char RCrt(volatile unsigned char * board, short idx);
inline unsigned char RGfx(volatile unsigned char * board, short idx);
inline void cv64_write_port(unsigned short bits,
			    volatile unsigned char *board);
inline void cvscreen(int toggle, volatile unsigned char *board);
inline void gfx_on_off(int toggle, volatile unsigned char *board);
#if 0
unsigned short cv64_compute_clock(unsigned long freq);
int cv_has_4mb(volatile unsigned char * fb);
void cv64_board_init(void);
void cv64_load_video_mode(struct fb_var_screeninfo *video_mode);
#endif

void cvision_bitblt(u_short sx, u_short sy, u_short dx, u_short dy, u_short w,
		    u_short h);
void cvision_clear(u_short dx, u_short dy, u_short w, u_short h, u_short bg);
