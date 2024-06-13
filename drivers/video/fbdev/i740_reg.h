/**************************************************************************

Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors:
 *   Kevin E. Martin <kevin@precisioninsight.com>
 */

/* I/O register offsets */
#define SRX VGA_SEQ_I
#define GRX VGA_GFX_I
#define ARX VGA_ATT_IW
#define XRX 0x3D6
#define MRX 0x3D2

/* VGA Color Palette Registers */
#define DACMASK		0x3C6
#define DACSTATE	0x3C7
#define DACRX		0x3C7
#define DACWX		0x3C8
#define DACDATA		0x3C9

/* CRT Controller Registers (CRX) */
#define START_ADDR_HI		0x0C
#define START_ADDR_LO		0x0D
#define VERT_SYNC_END		0x11
#define EXT_VERT_TOTAL		0x30
#define EXT_VERT_DISPLAY	0x31
#define EXT_VERT_SYNC_START	0x32
#define EXT_VERT_BLANK_START	0x33
#define EXT_HORIZ_TOTAL		0x35
#define EXT_HORIZ_BLANK		0x39
#define EXT_START_ADDR		0x40
#define EXT_START_ADDR_ENABLE	0x80
#define EXT_OFFSET		0x41
#define EXT_START_ADDR_HI	0x42
#define INTERLACE_CNTL		0x70
#define INTERLACE_ENABLE	0x80
#define INTERLACE_DISABLE	0x00

/* Miscellaneous Output Register */
#define MSR_R		0x3CC
#define MSR_W		0x3C2
#define IO_ADDR_SELECT	0x01

#define MDA_BASE	0x3B0
#define CGA_BASE	0x3D0

/* System Configuration Extension Registers (XRX) */
#define IO_CTNL		0x09
#define EXTENDED_ATTR_CNTL	0x02
#define EXTENDED_CRTC_CNTL	0x01

#define ADDRESS_MAPPING	0x0A
#define PACKED_MODE_ENABLE	0x04
#define LINEAR_MODE_ENABLE	0x02
#define PAGE_MAPPING_ENABLE	0x01

#define BITBLT_CNTL	0x20
#define COLEXP_MODE		0x30
#define COLEXP_8BPP		0x00
#define COLEXP_16BPP		0x10
#define COLEXP_24BPP		0x20
#define COLEXP_RESERVED		0x30
#define CHIP_RESET		0x02
#define BITBLT_STATUS		0x01

#define DISPLAY_CNTL	0x40
#define VGA_WRAP_MODE		0x02
#define VGA_WRAP_AT_256KB	0x00
#define VGA_NO_WRAP		0x02
#define GUI_MODE		0x01
#define STANDARD_VGA_MODE	0x00
#define HIRES_MODE		0x01

#define DRAM_ROW_TYPE	0x50
#define DRAM_ROW_0		0x07
#define DRAM_ROW_0_SDRAM	0x00
#define DRAM_ROW_0_EMPTY	0x07
#define DRAM_ROW_1		0x38
#define DRAM_ROW_1_SDRAM	0x00
#define DRAM_ROW_1_EMPTY	0x38
#define DRAM_ROW_CNTL_LO 0x51
#define DRAM_CAS_LATENCY	0x10
#define DRAM_RAS_TIMING		0x08
#define DRAM_RAS_PRECHARGE	0x04
#define DRAM_ROW_CNTL_HI 0x52
#define DRAM_EXT_CNTL	0x53
#define DRAM_REFRESH_RATE	0x03
#define DRAM_REFRESH_DISABLE	0x00
#define DRAM_REFRESH_60HZ	0x01
#define DRAM_REFRESH_FAST_TEST	0x02
#define DRAM_REFRESH_RESERVED	0x03
#define DRAM_TIMING	0x54
#define DRAM_ROW_BNDRY_0 0x55
#define DRAM_ROW_BNDRY_1 0x56

#define DPMS_SYNC_SELECT 0x61
#define VSYNC_CNTL		0x08
#define VSYNC_ON		0x00
#define VSYNC_OFF		0x08
#define HSYNC_CNTL		0x02
#define HSYNC_ON		0x00
#define HSYNC_OFF		0x02

#define PIXPIPE_CONFIG_0 0x80
#define DAC_8_BIT		0x80
#define DAC_6_BIT		0x00
#define HW_CURSOR_ENABLE	0x10
#define EXTENDED_PALETTE	0x01

#define PIXPIPE_CONFIG_1 0x81
#define DISPLAY_COLOR_MODE	0x0F
#define DISPLAY_VGA_MODE	0x00
#define DISPLAY_8BPP_MODE	0x02
#define DISPLAY_15BPP_MODE	0x04
#define DISPLAY_16BPP_MODE	0x05
#define DISPLAY_24BPP_MODE	0x06
#define DISPLAY_32BPP_MODE	0x07

#define PIXPIPE_CONFIG_2 0x82
#define DISPLAY_GAMMA_ENABLE	0x08
#define DISPLAY_GAMMA_DISABLE	0x00
#define OVERLAY_GAMMA_ENABLE	0x04
#define OVERLAY_GAMMA_DISABLE	0x00

#define CURSOR_CONTROL	0xA0
#define CURSOR_ORIGIN_SCREEN	0x00
#define CURSOR_ORIGIN_DISPLAY	0x10
#define CURSOR_MODE		0x07
#define CURSOR_MODE_DISABLE	0x00
#define CURSOR_MODE_32_4C_AX	0x01
#define CURSOR_MODE_128_2C	0x02
#define CURSOR_MODE_128_1C	0x03
#define CURSOR_MODE_64_3C	0x04
#define CURSOR_MODE_64_4C_AX	0x05
#define CURSOR_MODE_64_4C	0x06
#define CURSOR_MODE_RESERVED	0x07
#define CURSOR_BASEADDR_LO 0xA2
#define CURSOR_BASEADDR_HI 0xA3
#define CURSOR_X_LO	0xA4
#define CURSOR_X_HI	0xA5
#define CURSOR_X_POS		0x00
#define CURSOR_X_NEG		0x80
#define CURSOR_Y_LO	0xA6
#define CURSOR_Y_HI	0xA7
#define CURSOR_Y_POS		0x00
#define CURSOR_Y_NEG		0x80

#define VCLK2_VCO_M	0xC8
#define VCLK2_VCO_N	0xC9
#define VCLK2_VCO_MN_MSBS 0xCA
#define VCO_N_MSBS		0x30
#define VCO_M_MSBS		0x03
#define VCLK2_VCO_DIV_SEL 0xCB
#define POST_DIV_SELECT		0x70
#define POST_DIV_1		0x00
#define POST_DIV_2		0x10
#define POST_DIV_4		0x20
#define POST_DIV_8		0x30
#define POST_DIV_16		0x40
#define POST_DIV_32		0x50
#define VCO_LOOP_DIV_BY_4M	0x00
#define VCO_LOOP_DIV_BY_16M	0x04
#define REF_CLK_DIV_BY_5	0x02
#define REF_DIV_4		0x00
#define REF_DIV_1		0x01

#define PLL_CNTL	0xCE
#define PLL_MEMCLK_SEL		0x03
#define PLL_MEMCLK__66667KHZ	0x00
#define PLL_MEMCLK__75000KHZ	0x01
#define PLL_MEMCLK__88889KHZ	0x02
#define PLL_MEMCLK_100000KHZ	0x03

/* Multimedia Extension Registers (MRX) */
#define ACQ_CNTL_1	0x02
#define ACQ_CNTL_2	0x03
#define FRAME_CAP_MODE		0x01
#define CONT_CAP_MODE		0x00
#define SINGLE_CAP_MODE		0x01
#define ACQ_CNTL_3	0x04
#define COL_KEY_CNTL_1		0x3C
#define BLANK_DISP_OVERLAY	0x20

/* FIFOs */
#define LP_FIFO		0x1000
#define HP_FIFO		0x2000
#define INSTPNT		0x3040
#define LP_FIFO_COUNT	0x3040
#define HP_FIFO_COUNT	0x3041

/* FIFO Commands */
#define CLIENT		0xE0000000
#define CLIENT_2D	0x60000000

/* Command Parser Mode Register */
#define COMPARS		0x3038
#define TWO_D_INST_DISABLE		0x08
#define THREE_D_INST_DISABLE		0x04
#define STATE_VAR_UPDATE_DISABLE	0x02
#define PAL_STIP_DISABLE		0x01

/* Interrupt Control Registers */
#define IER		0x3030
#define IIR		0x3032
#define IMR		0x3034
#define ISR		0x3036
#define VMIINTB_EVENT		0x2000
#define GPIO4_INT		0x1000
#define DISP_FLIP_EVENT		0x0800
#define DVD_PORT_DMA		0x0400
#define DISP_VBLANK		0x0200
#define FIFO_EMPTY_DMA_DONE	0x0100
#define INST_PARSER_ERROR	0x0080
#define USER_DEFINED		0x0040
#define BREAKPOINT		0x0020
#define DISP_HORIZ_COUNT	0x0010
#define DISP_VSYNC		0x0008
#define CAPTURE_HORIZ_COUNT	0x0004
#define CAPTURE_VSYNC		0x0002
#define THREE_D_PIPE_FLUSHED	0x0001

/* FIFO Watermark and Burst Length Control Register */
#define FWATER_BLC	0x00006000
#define LMI_BURST_LENGTH	0x7F000000
#define LMI_FIFO_WATERMARK	0x003F0000
#define AGP_BURST_LENGTH	0x00007F00
#define AGP_FIFO_WATERMARK	0x0000003F

/* BitBLT Registers */
#define SRC_DST_PITCH	0x00040000
#define DST_PITCH		0x1FFF0000
#define SRC_PITCH		0x00001FFF
#define COLEXP_BG_COLOR	0x00040004
#define COLEXP_FG_COLOR	0x00040008
#define MONO_SRC_CNTL	0x0004000C
#define MONO_USE_COLEXP		0x00000000
#define MONO_USE_SRCEXP		0x08000000
#define MONO_DATA_ALIGN		0x07000000
#define MONO_BIT_ALIGN		0x01000000
#define MONO_BYTE_ALIGN		0x02000000
#define MONO_WORD_ALIGN		0x03000000
#define MONO_DWORD_ALIGN	0x04000000
#define MONO_QWORD_ALIGN	0x05000000
#define MONO_SRC_INIT_DSCRD	0x003F0000
#define MONO_SRC_RIGHT_CLIP	0x00003F00
#define MONO_SRC_LEFT_CLIP	0x0000003F
#define BITBLT_CONTROL	0x00040010
#define BLTR_STATUS		0x80000000
#define DYN_DEPTH		0x03000000
#define DYN_DEPTH_8BPP		0x00000000
#define DYN_DEPTH_16BPP		0x01000000
#define DYN_DEPTH_24BPP		0x02000000
#define DYN_DEPTH_32BPP		0x03000000	/* Unimplemented on the i740 */
#define DYN_DEPTH_ENABLE	0x00800000
#define PAT_VERT_ALIGN		0x00700000
#define SOLID_PAT_SELECT	0x00080000
#define PAT_IS_IN_COLOR		0x00000000
#define PAT_IS_MONO		0x00040000
#define MONO_PAT_TRANSP		0x00020000
#define COLOR_TRANSP_ROP	0x00000000
#define COLOR_TRANSP_DST	0x00008000
#define COLOR_TRANSP_EQ		0x00000000
#define COLOR_TRANSP_NOT_EQ	0x00010000
#define COLOR_TRANSP_ENABLE	0x00004000
#define MONO_SRC_TRANSP		0x00002000
#define SRC_IS_IN_COLOR		0x00000000
#define SRC_IS_MONO		0x00001000
#define SRC_USE_SRC_ADDR	0x00000000
#define SRC_USE_BLTDATA		0x00000400
#define BLT_TOP_TO_BOT		0x00000000
#define BLT_BOT_TO_TOP		0x00000200
#define BLT_LEFT_TO_RIGHT	0x00000000
#define BLT_RIGHT_TO_LEFT	0x00000100
#define BLT_ROP			0x000000FF
#define BLT_PAT_ADDR	0x00040014
#define BLT_SRC_ADDR	0x00040018
#define BLT_DST_ADDR	0x0004001C
#define BLT_DST_H_W	0x00040020
#define BLT_DST_HEIGHT		0x1FFF0000
#define BLT_DST_WIDTH		0x00001FFF
#define SRCEXP_BG_COLOR	0x00040024
#define SRCEXP_FG_COLOR	0x00040028
#define BLTDATA		0x00050000
