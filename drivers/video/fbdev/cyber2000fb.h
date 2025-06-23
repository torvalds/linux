/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/drivers/video/cyber2000fb.h
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 * Integraphics Cyber2000 frame buffer device
 */

/*
 * Internal CyberPro sizes and offsets.
 */
#define MMIO_OFFSET	0x00800000
#define MMIO_SIZE	0x000c0000

#define NR_PALETTE	256

#if defined(DEBUG) && defined(CONFIG_DEBUG_LL)
static void debug_printf(char *fmt, ...)
{
	extern void printascii(const char *);
	char buffer[128];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	va_end(ap);

	printascii(buffer);
}
#else
#define debug_printf(x...) do { } while (0)
#endif

#define RAMDAC_RAMPWRDN		0x01
#define RAMDAC_DAC8BIT		0x02
#define RAMDAC_VREFEN		0x04
#define RAMDAC_BYPASS		0x10
#define RAMDAC_DACPWRDN		0x40

#define EXT_CRT_VRTOFL		0x11
#define EXT_CRT_VRTOFL_LINECOMP10	0x10
#define EXT_CRT_VRTOFL_INTERLACE	0x20

#define EXT_CRT_IRQ		0x12
#define EXT_CRT_IRQ_ENABLE		0x01
#define EXT_CRT_IRQ_ACT_HIGH		0x04

#define EXT_CRT_TEST		0x13

#define EXT_SYNC_CTL		0x16
#define EXT_SYNC_CTL_HS_NORMAL		0x00
#define EXT_SYNC_CTL_HS_0		0x01
#define EXT_SYNC_CTL_HS_1		0x02
#define EXT_SYNC_CTL_HS_HSVS		0x03
#define EXT_SYNC_CTL_VS_NORMAL		0x00
#define EXT_SYNC_CTL_VS_0		0x04
#define EXT_SYNC_CTL_VS_1		0x08
#define EXT_SYNC_CTL_VS_COMP		0x0c

#define EXT_BUS_CTL		0x30
#define EXT_BUS_CTL_LIN_1MB		0x00
#define EXT_BUS_CTL_LIN_2MB		0x01
#define EXT_BUS_CTL_LIN_4MB		0x02
#define EXT_BUS_CTL_ZEROWAIT		0x04
#define EXT_BUS_CTL_PCIBURST_WRITE	0x20
#define EXT_BUS_CTL_PCIBURST_READ	0x80	/* CyberPro 5000 only */

#define EXT_SEG_WRITE_PTR	0x31
#define EXT_SEG_READ_PTR	0x32
#define EXT_BIU_MISC		0x33
#define EXT_BIU_MISC_LIN_ENABLE		0x01
#define EXT_BIU_MISC_COP_ENABLE		0x04
#define EXT_BIU_MISC_COP_BFC		0x08

#define EXT_FUNC_CTL		0x3c
#define EXT_FUNC_CTL_EXTREGENBL		0x80	/* enable access to 0xbcxxx		*/

#define PCI_BM_CTL		0x3e
#define PCI_BM_CTL_ENABLE		0x01	/* enable bus-master			*/
#define PCI_BM_CTL_BURST		0x02	/* enable burst				*/
#define PCI_BM_CTL_BACK2BACK		0x04	/* enable back to back			*/
#define PCI_BM_CTL_DUMMY		0x08	/* insert dummy cycle			*/

#define X_V2_VID_MEM_START	0x40
#define X_V2_VID_SRC_WIDTH	0x43
#define X_V2_X_START		0x45
#define X_V2_X_END		0x47
#define X_V2_Y_START		0x49
#define X_V2_Y_END		0x4b
#define X_V2_VID_SRC_WIN_WIDTH	0x4d

#define Y_V2_DDA_X_INC		0x43
#define Y_V2_DDA_Y_INC		0x47
#define Y_V2_VID_FIFO_CTL	0x49
#define Y_V2_VID_FMT		0x4b
#define Y_V2_VID_DISP_CTL1	0x4c
#define Y_V2_VID_FIFO_CTL1	0x4d

#define J_X2_VID_MEM_START	0x40
#define J_X2_VID_SRC_WIDTH	0x43
#define J_X2_X_START		0x47
#define J_X2_X_END		0x49
#define J_X2_Y_START		0x4b
#define J_X2_Y_END		0x4d
#define J_X2_VID_SRC_WIN_WIDTH	0x4f

#define K_X2_DDA_X_INIT		0x40
#define K_X2_DDA_X_INC		0x42
#define K_X2_DDA_Y_INIT		0x44
#define K_X2_DDA_Y_INC		0x46
#define K_X2_VID_FMT		0x48
#define K_X2_VID_DISP_CTL1	0x49

#define K_CAP_X2_CTL1		0x49

#define CURS_H_START		0x50
#define CURS_H_PRESET		0x52
#define CURS_V_START		0x53
#define CURS_V_PRESET		0x55
#define CURS_CTL		0x56

#define EXT_ATTRIB_CTL		0x57
#define EXT_ATTRIB_CTL_EXT		0x01

#define EXT_OVERSCAN_RED	0x58
#define EXT_OVERSCAN_GREEN	0x59
#define EXT_OVERSCAN_BLUE	0x5a

#define CAP_X_START		0x60
#define CAP_X_END		0x62
#define CAP_Y_START		0x64
#define CAP_Y_END		0x66
#define CAP_DDA_X_INIT		0x68
#define CAP_DDA_X_INC		0x6a
#define CAP_DDA_Y_INIT		0x6c
#define CAP_DDA_Y_INC		0x6e

#define EXT_MEM_CTL0		0x70
#define EXT_MEM_CTL0_7CLK		0x01
#define EXT_MEM_CTL0_RAS_1		0x02
#define EXT_MEM_CTL0_RAS2CAS_1		0x04
#define EXT_MEM_CTL0_MULTCAS		0x08
#define EXT_MEM_CTL0_ASYM		0x10
#define EXT_MEM_CTL0_CAS1ON		0x20
#define EXT_MEM_CTL0_FIFOFLUSH		0x40
#define EXT_MEM_CTL0_SEQRESET		0x80

#define EXT_MEM_CTL1		0x71
#define EXT_MEM_CTL1_PAR		0x00
#define EXT_MEM_CTL1_SERPAR		0x01
#define EXT_MEM_CTL1_SER		0x03
#define EXT_MEM_CTL1_SYNC		0x04
#define EXT_MEM_CTL1_VRAM		0x08
#define EXT_MEM_CTL1_4K_REFRESH		0x10
#define EXT_MEM_CTL1_256Kx4		0x00
#define EXT_MEM_CTL1_512Kx8		0x40
#define EXT_MEM_CTL1_1Mx16		0x60

#define EXT_MEM_CTL2		0x72
#define MEM_CTL2_SIZE_1MB		0x00
#define MEM_CTL2_SIZE_2MB		0x01
#define MEM_CTL2_SIZE_4MB		0x02
#define MEM_CTL2_SIZE_MASK		0x03
#define MEM_CTL2_64BIT			0x04

#define EXT_HIDDEN_CTL1		0x73

#define EXT_FIFO_CTL		0x74

#define EXT_SEQ_MISC		0x77
#define EXT_SEQ_MISC_8			0x01
#define EXT_SEQ_MISC_16_RGB565		0x02
#define EXT_SEQ_MISC_32			0x03
#define EXT_SEQ_MISC_24_RGB888		0x04
#define EXT_SEQ_MISC_16_RGB555		0x06
#define EXT_SEQ_MISC_8_RGB332		0x09
#define EXT_SEQ_MISC_16_RGB444		0x0a

#define EXT_HIDDEN_CTL4		0x7a

#define CURS_MEM_START		0x7e		/* bits 23..12 */

#define CAP_PIP_X_START		0x80
#define CAP_PIP_X_END		0x82
#define CAP_PIP_Y_START		0x84
#define CAP_PIP_Y_END		0x86

#define EXT_CAP_CTL1		0x88

#define EXT_CAP_CTL2		0x89
#define EXT_CAP_CTL2_ODDFRAMEIRQ	0x01
#define EXT_CAP_CTL2_ANYFRAMEIRQ	0x02

#define BM_CTRL0		0x9c
#define BM_CTRL1		0x9d

#define EXT_CAP_MODE1		0xa4
#define EXT_CAP_MODE1_8BIT		0x01	/* enable 8bit capture mode		*/
#define EXT_CAP_MODE1_CCIR656		0x02	/* CCIR656 mode				*/
#define EXT_CAP_MODE1_IGNOREVGT		0x04	/* ignore VGT				*/
#define EXT_CAP_MODE1_ALTFIFO		0x10	/* use alternate FIFO for capture	*/
#define EXT_CAP_MODE1_SWAPUV		0x20	/* swap UV bytes			*/
#define EXT_CAP_MODE1_MIRRORY		0x40	/* mirror vertically			*/
#define EXT_CAP_MODE1_MIRRORX		0x80	/* mirror horizontally			*/

#define EXT_CAP_MODE2		0xa5
#define EXT_CAP_MODE2_CCIRINVOE		0x01
#define EXT_CAP_MODE2_CCIRINVVGT	0x02
#define EXT_CAP_MODE2_CCIRINVHGT	0x04
#define EXT_CAP_MODE2_CCIRINVDG		0x08
#define EXT_CAP_MODE2_DATEND		0x10
#define EXT_CAP_MODE2_CCIRDGH		0x20
#define EXT_CAP_MODE2_FIXSONY		0x40
#define EXT_CAP_MODE2_SYNCFREEZE	0x80

#define EXT_TV_CTL		0xae

#define EXT_DCLK_MULT		0xb0
#define EXT_DCLK_DIV		0xb1
#define EXT_DCLK_DIV_VFSEL		0x20
#define EXT_MCLK_MULT		0xb2
#define EXT_MCLK_DIV		0xb3

#define EXT_LATCH1		0xb5
#define EXT_LATCH1_VAFC_EN		0x01	/* enable VAFC				*/

#define EXT_FEATURE		0xb7
#define EXT_FEATURE_BUS_MASK		0x07	/* host bus mask			*/
#define EXT_FEATURE_BUS_PCI		0x00
#define EXT_FEATURE_BUS_VL_STD		0x04
#define EXT_FEATURE_BUS_VL_LINEAR	0x05
#define EXT_FEATURE_1682		0x20	/* IGS 1682 compatibility		*/

#define EXT_LATCH2		0xb6
#define EXT_LATCH2_I2C_CLKEN		0x10
#define EXT_LATCH2_I2C_CLK		0x20
#define EXT_LATCH2_I2C_DATEN		0x40
#define EXT_LATCH2_I2C_DAT		0x80

#define EXT_XT_CTL		0xbe
#define EXT_XT_CAP16			0x04
#define EXT_XT_LINEARFB			0x08
#define EXT_XT_PAL			0x10

#define EXT_MEM_START		0xc0		/* ext start address 21 bits		*/
#define HOR_PHASE_SHIFT		0xc2		/* high 3 bits				*/
#define EXT_SRC_WIDTH		0xc3		/* ext offset phase  10 bits		*/
#define EXT_SRC_HEIGHT		0xc4		/* high 6 bits				*/
#define EXT_X_START		0xc5		/* ext->screen, 16 bits			*/
#define EXT_X_END		0xc7		/* ext->screen, 16 bits			*/
#define EXT_Y_START		0xc9		/* ext->screen, 16 bits			*/
#define EXT_Y_END		0xcb		/* ext->screen, 16 bits			*/
#define EXT_SRC_WIN_WIDTH	0xcd		/* 8 bits				*/
#define EXT_COLOUR_COMPARE	0xce		/* 24 bits				*/
#define EXT_DDA_X_INIT		0xd1		/* ext->screen 16 bits			*/
#define EXT_DDA_X_INC		0xd3		/* ext->screen 16 bits			*/
#define EXT_DDA_Y_INIT		0xd5		/* ext->screen 16 bits			*/
#define EXT_DDA_Y_INC		0xd7		/* ext->screen 16 bits			*/

#define EXT_VID_FIFO_CTL	0xd9

#define EXT_VID_FMT		0xdb
#define EXT_VID_FMT_YUV422		0x00	/* formats - does this cause conversion? */
#define EXT_VID_FMT_RGB555		0x01
#define EXT_VID_FMT_RGB565		0x02
#define EXT_VID_FMT_RGB888_24		0x03
#define EXT_VID_FMT_RGB888_32		0x04
#define EXT_VID_FMT_RGB8		0x05
#define EXT_VID_FMT_RGB4444		0x06
#define EXT_VID_FMT_RGB8T		0x07
#define EXT_VID_FMT_DUP_PIX_ZOON	0x08	/* duplicate pixel zoom			*/
#define EXT_VID_FMT_MOD_3RD_PIX		0x20	/* modify 3rd duplicated pixel		*/
#define EXT_VID_FMT_DBL_H_PIX		0x40	/* double horiz pixels			*/
#define EXT_VID_FMT_YUV128		0x80	/* YUV data offset by 128		*/

#define EXT_VID_DISP_CTL1	0xdc
#define EXT_VID_DISP_CTL1_INTRAM	0x01	/* video pixels go to internal RAM	*/
#define EXT_VID_DISP_CTL1_IGNORE_CCOMP	0x02	/* ignore colour compare registers	*/
#define EXT_VID_DISP_CTL1_NOCLIP	0x04	/* do not clip to 16235,16240		*/
#define EXT_VID_DISP_CTL1_UV_AVG	0x08	/* U/V data is averaged			*/
#define EXT_VID_DISP_CTL1_Y128		0x10	/* Y data offset by 128 (if YUV128 set)	*/
#define EXT_VID_DISP_CTL1_VINTERPOL_OFF	0x20	/* disable vertical interpolation	*/
#define EXT_VID_DISP_CTL1_FULL_WIN	0x40	/* video out window full		*/
#define EXT_VID_DISP_CTL1_ENABLE_WINDOW	0x80	/* enable video window			*/

#define EXT_VID_FIFO_CTL1	0xdd
#define EXT_VID_FIFO_CTL1_OE_HIGH	0x02
#define EXT_VID_FIFO_CTL1_INTERLEAVE	0x04	/* enable interleaved memory read	*/

#define EXT_ROM_UCB4GH		0xe5
#define EXT_ROM_UCB4GH_FREEZE		0x02	/* capture frozen			*/
#define EXT_ROM_UCB4GH_ODDFRAME		0x04	/* 1 = odd frame captured		*/
#define EXT_ROM_UCB4GH_1HL		0x08	/* first horizonal line after VGT falling edge */
#define EXT_ROM_UCB4GH_ODD		0x10	/* odd frame indicator			*/
#define EXT_ROM_UCB4GH_INTSTAT		0x20	/* video interrupt			*/

#define VFAC_CTL1		0xe8
#define VFAC_CTL1_CAPTURE		0x01	/* capture enable (only when VSYNC high)*/
#define VFAC_CTL1_VFAC_ENABLE		0x02	/* vfac enable				*/
#define VFAC_CTL1_FREEZE_CAPTURE	0x04	/* freeze capture			*/
#define VFAC_CTL1_FREEZE_CAPTURE_SYNC	0x08	/* sync freeze capture			*/
#define VFAC_CTL1_VALIDFRAME_SRC	0x10	/* select valid frame source		*/
#define VFAC_CTL1_PHILIPS		0x40	/* select Philips mode			*/
#define VFAC_CTL1_MODVINTERPOLCLK	0x80	/* modify vertical interpolation clocl	*/

#define VFAC_CTL2		0xe9
#define VFAC_CTL2_INVERT_VIDDATAVALID	0x01	/* invert video data valid		*/
#define VFAC_CTL2_INVERT_GRAPHREADY	0x02	/* invert graphic ready output sig	*/
#define VFAC_CTL2_INVERT_DATACLK	0x04	/* invert data clock signal		*/
#define VFAC_CTL2_INVERT_HSYNC		0x08	/* invert hsync input			*/
#define VFAC_CTL2_INVERT_VSYNC		0x10	/* invert vsync input			*/
#define VFAC_CTL2_INVERT_FRAME		0x20	/* invert frame odd/even input		*/
#define VFAC_CTL2_INVERT_BLANK		0x40	/* invert blank output			*/
#define VFAC_CTL2_INVERT_OVSYNC		0x80	/* invert other vsync input		*/

#define VFAC_CTL3		0xea
#define VFAC_CTL3_CAP_LARGE_FIFO	0x01	/* large capture fifo			*/
#define VFAC_CTL3_CAP_INTERLACE		0x02	/* capture odd and even fields		*/
#define VFAC_CTL3_CAP_HOLD_4NS		0x00	/* hold capture data for 4ns		*/
#define VFAC_CTL3_CAP_HOLD_2NS		0x04	/* hold capture data for 2ns		*/
#define VFAC_CTL3_CAP_HOLD_6NS		0x08	/* hold capture data for 6ns		*/
#define VFAC_CTL3_CAP_HOLD_0NS		0x0c	/* hold capture data for 0ns		*/
#define VFAC_CTL3_CHROMAKEY		0x20	/* capture data will be chromakeyed	*/
#define VFAC_CTL3_CAP_IRQ		0x40	/* enable capture interrupt		*/

#define CAP_MEM_START		0xeb		/* 18 bits				*/
#define CAP_MAP_WIDTH		0xed		/* high 6 bits				*/
#define CAP_PITCH		0xee		/* 8 bits				*/

#define CAP_CTL_MISC		0xef
#define CAP_CTL_MISC_HDIV		0x01
#define CAP_CTL_MISC_HDIV4		0x02
#define CAP_CTL_MISC_ODDEVEN		0x04
#define CAP_CTL_MISC_HSYNCDIV2		0x08
#define CAP_CTL_MISC_SYNCTZHIGH		0x10
#define CAP_CTL_MISC_SYNCTZOR		0x20
#define CAP_CTL_MISC_DISPUSED		0x80

#define REG_BANK		0xfa
#define REG_BANK_X			0x00
#define REG_BANK_Y			0x01
#define REG_BANK_W			0x02
#define REG_BANK_T			0x03
#define REG_BANK_J			0x04
#define REG_BANK_K			0x05

/*
 * Bus-master
 */
#define BM_VID_ADDR_LOW		0xbc040
#define BM_VID_ADDR_HIGH	0xbc044
#define BM_ADDRESS_LOW		0xbc080
#define BM_ADDRESS_HIGH		0xbc084
#define BM_LENGTH		0xbc088
#define BM_CONTROL		0xbc08c
#define BM_CONTROL_ENABLE		0x01	/* enable transfer			*/
#define BM_CONTROL_IRQEN		0x02	/* enable IRQ at end of transfer	*/
#define BM_CONTROL_INIT			0x04	/* initialise status & count		*/
#define BM_COUNT		0xbc090		/* read-only				*/

/*
 * TV registers
 */
#define TV_VBLANK_EVEN_START	0xbe43c
#define TV_VBLANK_EVEN_END	0xbe440
#define TV_VBLANK_ODD_START	0xbe444
#define TV_VBLANK_ODD_END	0xbe448
#define TV_SYNC_YGAIN		0xbe44c
#define TV_UV_GAIN		0xbe450
#define TV_PED_UVDET		0xbe454
#define TV_UV_BURST_AMP		0xbe458
#define TV_HSYNC_START		0xbe45c
#define TV_HSYNC_END		0xbe460
#define TV_Y_DELAY1		0xbe464
#define TV_Y_DELAY2		0xbe468
#define TV_UV_DELAY1		0xbe46c
#define TV_BURST_START		0xbe470
#define TV_BURST_END		0xbe474
#define TV_HBLANK_START		0xbe478
#define TV_HBLANK_END		0xbe47c
#define TV_PED_EVEN_START	0xbe480
#define TV_PED_EVEN_END		0xbe484
#define TV_PED_ODD_START	0xbe488
#define TV_PED_ODD_END		0xbe48c
#define TV_VSYNC_EVEN_START	0xbe490
#define TV_VSYNC_EVEN_END	0xbe494
#define TV_VSYNC_ODD_START	0xbe498
#define TV_VSYNC_ODD_END	0xbe49c
#define TV_SCFL			0xbe4a0
#define TV_SCFH			0xbe4a4
#define TV_SCP			0xbe4a8
#define TV_DELAYBYPASS		0xbe4b4
#define TV_EQL_END		0xbe4bc
#define TV_SERR_START		0xbe4c0
#define TV_SERR_END		0xbe4c4
#define TV_CTL			0xbe4dc	/* reflects a previous register- MVFCLR, MVPCLR etc P241*/
#define TV_VSYNC_VGA_HS		0xbe4e8
#define TV_FLICK_XMIN		0xbe514
#define TV_FLICK_XMAX		0xbe518
#define TV_FLICK_YMIN		0xbe51c
#define TV_FLICK_YMAX		0xbe520

/*
 * Graphics Co-processor
 */
#define CO_REG_CONTROL		0xbf011
#define CO_CTRL_BUSY			0x80
#define CO_CTRL_CMDFULL			0x04
#define CO_CTRL_FIFOEMPTY		0x02
#define CO_CTRL_READY			0x01

#define CO_REG_SRC_WIDTH	0xbf018
#define CO_REG_PIXFMT		0xbf01c
#define CO_PIXFMT_32BPP			0x03
#define CO_PIXFMT_24BPP			0x02
#define CO_PIXFMT_16BPP			0x01
#define CO_PIXFMT_8BPP			0x00

#define CO_REG_FGMIX		0xbf048
#define CO_FG_MIX_ZERO			0x00
#define CO_FG_MIX_SRC_AND_DST		0x01
#define CO_FG_MIX_SRC_AND_NDST		0x02
#define CO_FG_MIX_SRC			0x03
#define CO_FG_MIX_NSRC_AND_DST		0x04
#define CO_FG_MIX_DST			0x05
#define CO_FG_MIX_SRC_XOR_DST		0x06
#define CO_FG_MIX_SRC_OR_DST		0x07
#define CO_FG_MIX_NSRC_AND_NDST		0x08
#define CO_FG_MIX_SRC_XOR_NDST		0x09
#define CO_FG_MIX_NDST			0x0a
#define CO_FG_MIX_SRC_OR_NDST		0x0b
#define CO_FG_MIX_NSRC			0x0c
#define CO_FG_MIX_NSRC_OR_DST		0x0d
#define CO_FG_MIX_NSRC_OR_NDST		0x0e
#define CO_FG_MIX_ONES			0x0f

#define CO_REG_FGCOLOUR		0xbf058
#define CO_REG_BGCOLOUR		0xbf05c
#define CO_REG_PIXWIDTH		0xbf060
#define CO_REG_PIXHEIGHT	0xbf062
#define CO_REG_X_PHASE		0xbf078
#define CO_REG_CMD_L		0xbf07c
#define CO_CMD_L_PATTERN_FGCOL		0x8000
#define CO_CMD_L_INC_LEFT		0x0004
#define CO_CMD_L_INC_UP			0x0002

#define CO_REG_CMD_H		0xbf07e
#define CO_CMD_H_BGSRCMAP		0x8000	/* otherwise bg colour */
#define CO_CMD_H_FGSRCMAP		0x2000	/* otherwise fg colour */
#define CO_CMD_H_BLITTER		0x0800

#define CO_REG_SRC1_PTR		0xbf170
#define CO_REG_SRC2_PTR		0xbf174
#define CO_REG_DEST_PTR		0xbf178
#define CO_REG_DEST_WIDTH	0xbf218

/*
 * Private structure
 */
struct cfb_info;

struct cyberpro_info {
	struct device	*dev;
	struct i2c_adapter *i2c;
	unsigned char	__iomem *regs;
	char		__iomem *fb;
	char		dev_name[32];
	unsigned int	fb_size;
	unsigned int	chip_id;
	unsigned int	irq;

	/*
	 * The following is a pointer to be passed into the
	 * functions below.  The modules outside the main
	 * cyber2000fb.c driver have no knowledge as to what
	 * is within this structure.
	 */
	struct cfb_info *info;
};

#define ID_IGA_1682		0
#define ID_CYBERPRO_2000	1
#define ID_CYBERPRO_2010	2
#define ID_CYBERPRO_5000	3

/*
 * Note! Writing to the Cyber20x0 registers from an interrupt
 * routine is definitely a bad idea atm.
 */
void cyber2000fb_enable_extregs(struct cfb_info *cfb);
void cyber2000fb_disable_extregs(struct cfb_info *cfb);
