/*
 * MGA Millennium (MGA2064W) functions
 * MGA Mystique (MGA1064SG) functions
 *
 * Copyright 1996 The XFree86 Project, Inc.
 *
 * Authors
 *		Dirk Hohndel
 *			hohndel@XFree86.Org
 *		David Dawes
 *			dawes@XFree86.Org
 * Contributors:
 *		Guy DESBIEF, Aix-en-provence, France
 *			g.desbief@aix.pacwan.net
 *		MGA1064SG Mystique register file
 */


#ifndef _MGA_REG_H_
#define _MGA_REG_H_

#define	MGAREG_DWGCTL		0x1c00
#define	MGAREG_MACCESS		0x1c04
/* the following is a mystique only register */
#define MGAREG_MCTLWTST		0x1c08
#define	MGAREG_ZORG		0x1c0c

#define	MGAREG_PAT0		0x1c10
#define	MGAREG_PAT1		0x1c14
#define	MGAREG_PLNWT		0x1c1c

#define	MGAREG_BCOL		0x1c20
#define	MGAREG_FCOL		0x1c24

#define	MGAREG_SRC0		0x1c30
#define	MGAREG_SRC1		0x1c34
#define	MGAREG_SRC2		0x1c38
#define	MGAREG_SRC3		0x1c3c

#define	MGAREG_XYSTRT		0x1c40
#define	MGAREG_XYEND		0x1c44

#define	MGAREG_SHIFT		0x1c50
/* the following is a mystique only register */
#define MGAREG_DMAPAD		0x1c54
#define	MGAREG_SGN		0x1c58
#define	MGAREG_LEN		0x1c5c

#define	MGAREG_AR0		0x1c60
#define	MGAREG_AR1		0x1c64
#define	MGAREG_AR2		0x1c68
#define	MGAREG_AR3		0x1c6c
#define	MGAREG_AR4		0x1c70
#define	MGAREG_AR5		0x1c74
#define	MGAREG_AR6		0x1c78

#define	MGAREG_CXBNDRY		0x1c80
#define	MGAREG_FXBNDRY		0x1c84
#define	MGAREG_YDSTLEN		0x1c88
#define	MGAREG_PITCH		0x1c8c

#define	MGAREG_YDST		0x1c90
#define	MGAREG_YDSTORG		0x1c94
#define	MGAREG_YTOP		0x1c98
#define	MGAREG_YBOT		0x1c9c

#define	MGAREG_CXLEFT		0x1ca0
#define	MGAREG_CXRIGHT		0x1ca4
#define	MGAREG_FXLEFT		0x1ca8
#define	MGAREG_FXRIGHT		0x1cac

#define	MGAREG_XDST		0x1cb0

#define	MGAREG_DR0		0x1cc0
#define	MGAREG_DR1		0x1cc4
#define	MGAREG_DR2		0x1cc8
#define	MGAREG_DR3		0x1ccc

#define	MGAREG_DR4		0x1cd0
#define	MGAREG_DR5		0x1cd4
#define	MGAREG_DR6		0x1cd8
#define	MGAREG_DR7		0x1cdc

#define	MGAREG_DR8		0x1ce0
#define	MGAREG_DR9		0x1ce4
#define	MGAREG_DR10		0x1ce8
#define	MGAREG_DR11		0x1cec

#define	MGAREG_DR12		0x1cf0
#define	MGAREG_DR13		0x1cf4
#define	MGAREG_DR14		0x1cf8
#define	MGAREG_DR15		0x1cfc

#define MGAREG_SRCORG		0x2cb4
#define MGAREG_DSTORG		0x2cb8

/* add or or this to one of the previous "power registers" to start
   the drawing engine */

#define MGAREG_EXEC		0x0100

#define	MGAREG_FIFOSTATUS	0x1e10
#define	MGAREG_Status		0x1e14
#define MGAREG_CACHEFLUSH       0x1fff
#define	MGAREG_ICLEAR		0x1e18
#define	MGAREG_IEN		0x1e1c

#define	MGAREG_VCOUNT		0x1e20

#define	MGAREG_Reset		0x1e40

#define	MGAREG_OPMODE		0x1e54

/* Warp Registers */
#define MGAREG_WIADDR           0x1dc0
#define MGAREG_WIADDR2          0x1dd8
#define MGAREG_WGETMSB          0x1dc8
#define MGAREG_WVRTXSZ          0x1dcc
#define MGAREG_WACCEPTSEQ       0x1dd4
#define MGAREG_WMISC            0x1e70

#define MGAREG_MEMCTL           0x2e08

/* OPMODE register additives */

#define MGAOPM_DMA_GENERAL	(0x00 << 2)
#define MGAOPM_DMA_BLIT		(0x01 << 2)
#define MGAOPM_DMA_VECTOR	(0x10 << 2)

/* MACCESS register additives */
#define MGAMAC_PW8               0x00
#define MGAMAC_PW16              0x01
#define MGAMAC_PW24              0x03 /* not a typo */
#define MGAMAC_PW32              0x02 /* not a typo */
#define MGAMAC_BYPASS332         0x10000000
#define MGAMAC_NODITHER          0x40000000
#define MGAMAC_DIT555            0x80000000

/* DWGCTL register additives */

/* Lines */

#define MGADWG_LINE_OPEN	0x00
#define MGADWG_AUTOLINE_OPEN	0x01
#define MGADWG_LINE_CLOSE	0x02
#define MGADWG_AUTOLINE_CLOSE	0x03

/* Trapezoids */
#define MGADWG_TRAP		0x04
#define MGADWG_TEXTURE_TRAP	0x06

/* BitBlts */

#define MGADWG_BITBLT		0x08
#define MGADWG_FBITBLT		0x0c
#define MGADWG_ILOAD		0x09
#define MGADWG_ILOAD_SCALE	0x0d
#define MGADWG_ILOAD_FILTER	0x0f
#define MGADWG_ILOAD_HIQH	0x07
#define MGADWG_ILOAD_HIQHV	0x0e
#define MGADWG_IDUMP		0x0a

/* atype access to WRAM */

#define MGADWG_RPL		( 0x00 << 4 )
#define MGADWG_RSTR		( 0x01 << 4 )
#define MGADWG_ZI		( 0x03 << 4 )
#define MGADWG_BLK 		( 0x04 << 4 )
#define MGADWG_I		( 0x07 << 4 )

/* specifies whether bit blits are linear or xy */
#define MGADWG_LINEAR		( 0x01 << 7 )

/* z drawing mode. use MGADWG_NOZCMP for always */

#define MGADWG_NOZCMP		( 0x00 << 8 )
#define MGADWG_ZE		( 0x02 << 8 )
#define MGADWG_ZNE		( 0x03 << 8 )
#define MGADWG_ZLT		( 0x04 << 8 )
#define MGADWG_ZLTE		( 0x05 << 8 )
#define MGADWG_GT		( 0x06 << 8 )
#define MGADWG_GTE		( 0x07 << 8 )

/* use this to force colour expansion circuitry to do its stuff */

#define MGADWG_SOLID		( 0x01 << 11 )

/* ar register at zero */

#define MGADWG_ARZERO		( 0x01 << 12 )

#define MGADWG_SGNZERO		( 0x01 << 13 )

#define MGADWG_SHIFTZERO	( 0x01 << 14 )

/* See table on 4-43 for bop ALU operations */

/* See table on 4-44 for translucidity masks */

#define MGADWG_BMONOLEF		( 0x00 << 25 )
#define MGADWG_BMONOWF		( 0x04 << 25 )
#define MGADWG_BPLAN		( 0x01 << 25 )

/* note that if bfcol is specified and you're doing a bitblt, it causes
   a fbitblt to be performed, so check that you obey the fbitblt rules */

#define MGADWG_BFCOL   		( 0x02 << 25 )
#define MGADWG_BUYUV		( 0x0e << 25 )
#define MGADWG_BU32BGR		( 0x03 << 25 )
#define MGADWG_BU32RGB		( 0x07 << 25 )
#define MGADWG_BU24BGR		( 0x0b << 25 )
#define MGADWG_BU24RGB		( 0x0f << 25 )

#define MGADWG_PATTERN		( 0x01 << 29 )
#define MGADWG_TRANSC		( 0x01 << 30 )
#define MGAREG_MISC_WRITE	0x3c2
#define MGAREG_MISC_READ	0x3cc
#define MGAREG_MEM_MISC_WRITE       0x1fc2
#define MGAREG_MEM_MISC_READ        0x1fcc

#define MGAREG_MISC_IOADSEL	(0x1 << 0)
#define MGAREG_MISC_RAMMAPEN	(0x1 << 1)
#define MGAREG_MISC_CLK_SEL_VGA25	(0x0 << 2)
#define MGAREG_MISC_CLK_SEL_VGA28	(0x1 << 2)
#define MGAREG_MISC_CLK_SEL_MGA_PIX	(0x2 << 2)
#define MGAREG_MISC_CLK_SEL_MGA_MSK	(0x3 << 2)
#define MGAREG_MISC_VIDEO_DIS	(0x1 << 4)
#define MGAREG_MISC_HIGH_PG_SEL	(0x1 << 5)

/* MMIO VGA registers */
#define MGAREG_SEQ_INDEX	0x1fc4
#define MGAREG_SEQ_DATA		0x1fc5
#define MGAREG_CRTC_INDEX	0x1fd4
#define MGAREG_CRTC_DATA	0x1fd5
#define MGAREG_CRTCEXT_INDEX	0x1fde
#define MGAREG_CRTCEXT_DATA	0x1fdf



/* MGA bits for registers PCI_OPTION_REG */
#define MGA1064_OPT_SYS_CLK_PCI   		( 0x00 << 0 )
#define MGA1064_OPT_SYS_CLK_PLL   		( 0x01 << 0 )
#define MGA1064_OPT_SYS_CLK_EXT   		( 0x02 << 0 )
#define MGA1064_OPT_SYS_CLK_MSK   		( 0x03 << 0 )

#define MGA1064_OPT_SYS_CLK_DIS   		( 0x01 << 2 )
#define MGA1064_OPT_G_CLK_DIV_1   		( 0x01 << 3 )
#define MGA1064_OPT_M_CLK_DIV_1   		( 0x01 << 4 )

#define MGA1064_OPT_SYS_PLL_PDN   		( 0x01 << 5 )
#define MGA1064_OPT_VGA_ION   		( 0x01 << 8 )

/* MGA registers in PCI config space */
#define PCI_MGA_INDEX		0x44
#define PCI_MGA_DATA		0x48
#define PCI_MGA_OPTION		0x40
#define PCI_MGA_OPTION2		0x50
#define PCI_MGA_OPTION3		0x54

#define RAMDAC_OFFSET		0x3c00

/* TVP3026 direct registers */

#define TVP3026_INDEX		0x00
#define TVP3026_WADR_PAL	0x00
#define TVP3026_COL_PAL		0x01
#define TVP3026_PIX_RD_MSK	0x02
#define TVP3026_RADR_PAL	0x03
#define TVP3026_CUR_COL_ADDR	0x04
#define TVP3026_CUR_COL_DATA	0x05
#define TVP3026_DATA		0x0a
#define TVP3026_CUR_RAM		0x0b
#define TVP3026_CUR_XLOW	0x0c
#define TVP3026_CUR_XHI		0x0d
#define TVP3026_CUR_YLOW	0x0e
#define TVP3026_CUR_YHI		0x0f

/* TVP3026 indirect registers */

#define TVP3026_SILICON_REV	0x01
#define TVP3026_CURSOR_CTL	0x06
#define TVP3026_LATCH_CTL	0x0f
#define TVP3026_TRUE_COLOR_CTL	0x18
#define TVP3026_MUX_CTL		0x19
#define TVP3026_CLK_SEL		0x1a
#define TVP3026_PAL_PAGE	0x1c
#define TVP3026_GEN_CTL		0x1d
#define TVP3026_MISC_CTL	0x1e
#define TVP3026_GEN_IO_CTL	0x2a
#define TVP3026_GEN_IO_DATA	0x2b
#define TVP3026_PLL_ADDR	0x2c
#define TVP3026_PIX_CLK_DATA	0x2d
#define TVP3026_MEM_CLK_DATA	0x2e
#define TVP3026_LOAD_CLK_DATA	0x2f
#define TVP3026_KEY_RED_LOW	0x32
#define TVP3026_KEY_RED_HI	0x33
#define TVP3026_KEY_GREEN_LOW	0x34
#define TVP3026_KEY_GREEN_HI	0x35
#define TVP3026_KEY_BLUE_LOW	0x36
#define TVP3026_KEY_BLUE_HI	0x37
#define TVP3026_KEY_CTL		0x38
#define TVP3026_MCLK_CTL	0x39
#define TVP3026_SENSE_TEST	0x3a
#define TVP3026_TEST_DATA	0x3b
#define TVP3026_CRC_LSB		0x3c
#define TVP3026_CRC_MSB		0x3d
#define TVP3026_CRC_CTL		0x3e
#define TVP3026_ID		0x3f
#define TVP3026_RESET		0xff


/* MGA1064 DAC Register file */
/* MGA1064 direct registers */

#define MGA1064_INDEX		0x00
#define MGA1064_WADR_PAL	0x00
#define MGA1064_SPAREREG        0x00
#define MGA1064_COL_PAL		0x01
#define MGA1064_PIX_RD_MSK	0x02
#define MGA1064_RADR_PAL	0x03
#define MGA1064_DATA		0x0a

#define MGA1064_CUR_XLOW	0x0c
#define MGA1064_CUR_XHI		0x0d
#define MGA1064_CUR_YLOW	0x0e
#define MGA1064_CUR_YHI		0x0f

/* MGA1064 indirect registers */
#define MGA1064_DVI_PIPE_CTL    0x03
#define MGA1064_CURSOR_BASE_ADR_LOW	0x04
#define MGA1064_CURSOR_BASE_ADR_HI	0x05
#define MGA1064_CURSOR_CTL	0x06
#define MGA1064_CURSOR_COL0_RED	0x08
#define MGA1064_CURSOR_COL0_GREEN	0x09
#define MGA1064_CURSOR_COL0_BLUE	0x0a

#define MGA1064_CURSOR_COL1_RED	0x0c
#define MGA1064_CURSOR_COL1_GREEN	0x0d
#define MGA1064_CURSOR_COL1_BLUE	0x0e

#define MGA1064_CURSOR_COL2_RED	0x010
#define MGA1064_CURSOR_COL2_GREEN	0x011
#define MGA1064_CURSOR_COL2_BLUE	0x012

#define MGA1064_VREF_CTL	0x018

#define MGA1064_MUL_CTL		0x19
#define MGA1064_MUL_CTL_8bits		0x0
#define MGA1064_MUL_CTL_15bits		0x01
#define MGA1064_MUL_CTL_16bits		0x02
#define MGA1064_MUL_CTL_24bits		0x03
#define MGA1064_MUL_CTL_32bits		0x04
#define MGA1064_MUL_CTL_2G8V16bits		0x05
#define MGA1064_MUL_CTL_G16V16bits		0x06
#define MGA1064_MUL_CTL_32_24bits		0x07

#define MGA1064_PIX_CLK_CTL		0x1a
#define MGA1064_PIX_CLK_CTL_CLK_DIS		( 0x01 << 2 )
#define MGA1064_PIX_CLK_CTL_CLK_POW_DOWN	( 0x01 << 3 )
#define MGA1064_PIX_CLK_CTL_SEL_PCI		( 0x00 << 0 )
#define MGA1064_PIX_CLK_CTL_SEL_PLL		( 0x01 << 0 )
#define MGA1064_PIX_CLK_CTL_SEL_EXT		( 0x02 << 0 )
#define MGA1064_PIX_CLK_CTL_SEL_MSK		( 0x03 << 0 )

#define MGA1064_GEN_CTL		0x1d
#define MGA1064_GEN_CTL_SYNC_ON_GREEN_DIS      (0x01 << 5)
#define MGA1064_MISC_CTL	0x1e
#define MGA1064_MISC_CTL_DAC_EN                ( 0x01 << 0 )
#define MGA1064_MISC_CTL_VGA   		( 0x01 << 1 )
#define MGA1064_MISC_CTL_DIS_CON   		( 0x03 << 1 )
#define MGA1064_MISC_CTL_MAFC   		( 0x02 << 1 )
#define MGA1064_MISC_CTL_VGA8   		( 0x01 << 3 )
#define MGA1064_MISC_CTL_DAC_RAM_CS   		( 0x01 << 4 )

#define MGA1064_GEN_IO_CTL2	0x29
#define MGA1064_GEN_IO_CTL	0x2a
#define MGA1064_GEN_IO_DATA	0x2b
#define MGA1064_SYS_PLL_M	0x2c
#define MGA1064_SYS_PLL_N	0x2d
#define MGA1064_SYS_PLL_P	0x2e
#define MGA1064_SYS_PLL_STAT	0x2f

#define MGA1064_REMHEADCTL     0x30
#define MGA1064_REMHEADCTL_CLKDIS ( 0x01 << 0 )
#define MGA1064_REMHEADCTL_CLKSL_OFF ( 0x00 << 1 )
#define MGA1064_REMHEADCTL_CLKSL_PLL ( 0x01 << 1 )
#define MGA1064_REMHEADCTL_CLKSL_PCI ( 0x02 << 1 )
#define MGA1064_REMHEADCTL_CLKSL_MSK ( 0x03 << 1 )

#define MGA1064_REMHEADCTL2     0x31

#define MGA1064_ZOOM_CTL	0x38
#define MGA1064_SENSE_TST	0x3a

#define MGA1064_CRC_LSB		0x3c
#define MGA1064_CRC_MSB		0x3d
#define MGA1064_CRC_CTL		0x3e
#define MGA1064_COL_KEY_MSK_LSB		0x40
#define MGA1064_COL_KEY_MSK_MSB		0x41
#define MGA1064_COL_KEY_LSB		0x42
#define MGA1064_COL_KEY_MSB		0x43
#define MGA1064_PIX_PLLA_M	0x44
#define MGA1064_PIX_PLLA_N	0x45
#define MGA1064_PIX_PLLA_P	0x46
#define MGA1064_PIX_PLLB_M	0x48
#define MGA1064_PIX_PLLB_N	0x49
#define MGA1064_PIX_PLLB_P	0x4a
#define MGA1064_PIX_PLLC_M	0x4c
#define MGA1064_PIX_PLLC_N	0x4d
#define MGA1064_PIX_PLLC_P	0x4e

#define MGA1064_PIX_PLL_STAT	0x4f

/*Added for G450 dual head*/

#define MGA1064_VID_PLL_STAT    0x8c
#define MGA1064_VID_PLL_P       0x8D
#define MGA1064_VID_PLL_M       0x8E
#define MGA1064_VID_PLL_N       0x8F

/* Modified PLL for G200 Winbond (G200WB) */
#define MGA1064_WB_PIX_PLLC_M	0xb7
#define MGA1064_WB_PIX_PLLC_N	0xb6
#define MGA1064_WB_PIX_PLLC_P	0xb8

/* Modified PLL for G200 Maxim (G200EV) */
#define MGA1064_EV_PIX_PLLC_M	0xb6
#define MGA1064_EV_PIX_PLLC_N	0xb7
#define MGA1064_EV_PIX_PLLC_P	0xb8

/* Modified PLL for G200 EH */
#define MGA1064_EH_PIX_PLLC_M   0xb6
#define MGA1064_EH_PIX_PLLC_N   0xb7
#define MGA1064_EH_PIX_PLLC_P   0xb8

/* Modified PLL for G200 Maxim (G200ER) */
#define MGA1064_ER_PIX_PLLC_M	0xb7
#define MGA1064_ER_PIX_PLLC_N	0xb6
#define MGA1064_ER_PIX_PLLC_P	0xb8

#define MGA1064_DISP_CTL        0x8a
#define MGA1064_DISP_CTL_DAC1OUTSEL_MASK       0x01
#define MGA1064_DISP_CTL_DAC1OUTSEL_DIS        0x00
#define MGA1064_DISP_CTL_DAC1OUTSEL_EN         0x01
#define MGA1064_DISP_CTL_DAC2OUTSEL_MASK       (0x03 << 2)
#define MGA1064_DISP_CTL_DAC2OUTSEL_DIS        0x00
#define MGA1064_DISP_CTL_DAC2OUTSEL_CRTC1      (0x01 << 2)
#define MGA1064_DISP_CTL_DAC2OUTSEL_CRTC2      (0x02 << 2)
#define MGA1064_DISP_CTL_DAC2OUTSEL_TVE        (0x03 << 2)
#define MGA1064_DISP_CTL_PANOUTSEL_MASK        (0x03 << 5)
#define MGA1064_DISP_CTL_PANOUTSEL_DIS         0x00
#define MGA1064_DISP_CTL_PANOUTSEL_CRTC1       (0x01 << 5)
#define MGA1064_DISP_CTL_PANOUTSEL_CRTC2RGB    (0x02 << 5)
#define MGA1064_DISP_CTL_PANOUTSEL_CRTC2656    (0x03 << 5)

#define MGA1064_SYNC_CTL        0x8b

#define MGA1064_PWR_CTL         0xa0
#define MGA1064_PWR_CTL_DAC2_EN                (0x01 << 0)
#define MGA1064_PWR_CTL_VID_PLL_EN             (0x01 << 1)
#define MGA1064_PWR_CTL_PANEL_EN               (0x01 << 2)
#define MGA1064_PWR_CTL_RFIFO_EN               (0x01 << 3)
#define MGA1064_PWR_CTL_CFIFO_EN               (0x01 << 4)

#define MGA1064_PAN_CTL         0xa2

/* Using crtc2 */
#define MGAREG2_C2CTL            0x10
#define MGAREG2_C2HPARAM         0x14
#define MGAREG2_C2HSYNC          0x18
#define MGAREG2_C2VPARAM         0x1c
#define MGAREG2_C2VSYNC          0x20
#define MGAREG2_C2STARTADD0      0x28

#define MGAREG2_C2OFFSET         0x40
#define MGAREG2_C2DATACTL        0x4c

#define MGAREG_C2CTL            0x3c10
#define MGAREG_C2CTL_C2_EN                     0x01

#define MGAREG_C2_HIPRILVL_M                   (0x07 << 4)
#define MGAREG_C2_MAXHIPRI_M                   (0x07 << 8)

#define MGAREG_C2CTL_PIXCLKSEL_MASK            (0x03 << 1)
#define MGAREG_C2CTL_PIXCLKSELH_MASK           (0x01 << 14)
#define MGAREG_C2CTL_PIXCLKSEL_PCICLK          0x00
#define MGAREG_C2CTL_PIXCLKSEL_VDOCLK          (0x01 << 1)
#define MGAREG_C2CTL_PIXCLKSEL_PIXELPLL        (0x02 << 1)
#define MGAREG_C2CTL_PIXCLKSEL_VIDEOPLL        (0x03 << 1)
#define MGAREG_C2CTL_PIXCLKSEL_VDCLK           (0x01 << 14)

#define MGAREG_C2CTL_PIXCLKSEL_CRISTAL         (0x01 << 1) | (0x01 << 14)
#define MGAREG_C2CTL_PIXCLKSEL_SYSTEMPLL       (0x02 << 1) | (0x01 << 14)

#define MGAREG_C2CTL_PIXCLKDIS_MASK            (0x01 << 3)
#define MGAREG_C2CTL_PIXCLKDIS_DISABLE         (0x01 << 3)

#define MGAREG_C2CTL_CRTCDACSEL_MASK           (0x01 << 20)
#define MGAREG_C2CTL_CRTCDACSEL_CRTC1          0x00
#define MGAREG_C2CTL_CRTCDACSEL_CRTC2          (0x01 << 20)

#define MGAREG_C2HPARAM         0x3c14
#define MGAREG_C2HSYNC          0x3c18
#define MGAREG_C2VPARAM         0x3c1c
#define MGAREG_C2VSYNC          0x3c20
#define MGAREG_C2STARTADD0      0x3c28

#define MGAREG_C2OFFSET         0x3c40
#define MGAREG_C2DATACTL        0x3c4c

/* video register */

#define MGAREG_BESA1C3ORG	0x3d60
#define MGAREG_BESA1CORG	0x3d10
#define MGAREG_BESA1ORG		0x3d00
#define MGAREG_BESCTL		0x3d20
#define MGAREG_BESGLOBCTL	0x3dc0
#define MGAREG_BESHCOORD	0x3d28
#define MGAREG_BESHISCAL	0x3d30
#define MGAREG_BESHSRCEND	0x3d3c
#define MGAREG_BESHSRCLST	0x3d50
#define MGAREG_BESHSRCST	0x3d38
#define MGAREG_BESLUMACTL	0x3d40
#define MGAREG_BESPITCH		0x3d24
#define MGAREG_BESV1SRCLST	0x3d54
#define MGAREG_BESV1WGHT	0x3d48
#define MGAREG_BESVCOORD	0x3d2c
#define MGAREG_BESVISCAL	0x3d34

/* texture engine registers */

#define MGAREG_TMR0		0x2c00
#define MGAREG_TMR1		0x2c04
#define MGAREG_TMR2		0x2c08
#define MGAREG_TMR3		0x2c0c
#define MGAREG_TMR4		0x2c10
#define MGAREG_TMR5		0x2c14
#define MGAREG_TMR6		0x2c18
#define MGAREG_TMR7		0x2c1c
#define MGAREG_TMR8		0x2c20
#define MGAREG_TEXORG		0x2c24
#define MGAREG_TEXWIDTH		0x2c28
#define MGAREG_TEXHEIGHT	0x2c2c
#define MGAREG_TEXCTL		0x2c30
#    define MGA_TW4                             (0x00000000)
#    define MGA_TW8                             (0x00000001)
#    define MGA_TW15                            (0x00000002)
#    define MGA_TW16                            (0x00000003)
#    define MGA_TW12                            (0x00000004)
#    define MGA_TW32                            (0x00000006)
#    define MGA_TW8A                            (0x00000007)
#    define MGA_TW8AL                           (0x00000008)
#    define MGA_TW422                           (0x0000000A)
#    define MGA_TW422UYVY                       (0x0000000B)
#    define MGA_PITCHLIN                        (0x00000100)
#    define MGA_NOPERSPECTIVE                   (0x00200000)
#    define MGA_TAKEY                           (0x02000000)
#    define MGA_TAMASK                          (0x04000000)
#    define MGA_CLAMPUV                         (0x18000000)
#    define MGA_TEXMODULATE                     (0x20000000)
#define MGAREG_TEXCTL2		0x2c3c
#    define MGA_G400_TC2_MAGIC                  (0x00008000)
#    define MGA_TC2_DECALBLEND                  (0x00000001)
#    define MGA_TC2_IDECAL                      (0x00000002)
#    define MGA_TC2_DECALDIS                    (0x00000004)
#    define MGA_TC2_CKSTRANSDIS                 (0x00000010)
#    define MGA_TC2_BORDEREN                    (0x00000020)
#    define MGA_TC2_SPECEN                      (0x00000040)
#    define MGA_TC2_DUALTEX                     (0x00000080)
#    define MGA_TC2_TABLEFOG                    (0x00000100)
#    define MGA_TC2_BUMPMAP                     (0x00000200)
#    define MGA_TC2_SELECT_TMU1                 (0x80000000)
#define MGAREG_TEXTRANS		0x2c34
#define MGAREG_TEXTRANSHIGH	0x2c38
#define MGAREG_TEXFILTER	0x2c58
#    define MGA_MIN_NRST                        (0x00000000)
#    define MGA_MIN_BILIN                       (0x00000002)
#    define MGA_MIN_ANISO                       (0x0000000D)
#    define MGA_MAG_NRST                        (0x00000000)
#    define MGA_MAG_BILIN                       (0x00000020)
#    define MGA_FILTERALPHA                     (0x00100000)
#define MGAREG_ALPHASTART	0x2c70
#define MGAREG_ALPHAXINC	0x2c74
#define MGAREG_ALPHAYINC	0x2c78
#define MGAREG_ALPHACTRL	0x2c7c
#    define MGA_SRC_ZERO                        (0x00000000)
#    define MGA_SRC_ONE                         (0x00000001)
#    define MGA_SRC_DST_COLOR                   (0x00000002)
#    define MGA_SRC_ONE_MINUS_DST_COLOR         (0x00000003)
#    define MGA_SRC_ALPHA                       (0x00000004)
#    define MGA_SRC_ONE_MINUS_SRC_ALPHA         (0x00000005)
#    define MGA_SRC_DST_ALPHA                   (0x00000006)
#    define MGA_SRC_ONE_MINUS_DST_ALPHA         (0x00000007)
#    define MGA_SRC_SRC_ALPHA_SATURATE          (0x00000008)
#    define MGA_SRC_BLEND_MASK                  (0x0000000f)
#    define MGA_DST_ZERO                        (0x00000000)
#    define MGA_DST_ONE                         (0x00000010)
#    define MGA_DST_SRC_COLOR                   (0x00000020)
#    define MGA_DST_ONE_MINUS_SRC_COLOR         (0x00000030)
#    define MGA_DST_SRC_ALPHA                   (0x00000040)
#    define MGA_DST_ONE_MINUS_SRC_ALPHA         (0x00000050)
#    define MGA_DST_DST_ALPHA                   (0x00000060)
#    define MGA_DST_ONE_MINUS_DST_ALPHA         (0x00000070)
#    define MGA_DST_BLEND_MASK                  (0x00000070)
#    define MGA_ALPHACHANNEL                    (0x00000100)
#    define MGA_VIDEOALPHA                      (0x00000200)
#    define MGA_DIFFUSEDALPHA                   (0x01000000)
#    define MGA_MODULATEDALPHA                  (0x02000000)
#define MGAREG_TDUALSTAGE0                      (0x2CF8)
#define MGAREG_TDUALSTAGE1                      (0x2CFC)
#    define MGA_TDS_COLOR_ARG2_DIFFUSE          (0x00000000)
#    define MGA_TDS_COLOR_ARG2_SPECULAR         (0x00000001)
#    define MGA_TDS_COLOR_ARG2_FCOL             (0x00000002)
#    define MGA_TDS_COLOR_ARG2_PREVSTAGE        (0x00000003)
#    define MGA_TDS_COLOR_ALPHA_DIFFUSE         (0x00000000)
#    define MGA_TDS_COLOR_ALPHA_FCOL            (0x00000004)
#    define MGA_TDS_COLOR_ALPHA_CURRTEX         (0x00000008)
#    define MGA_TDS_COLOR_ALPHA_PREVTEX         (0x0000000c)
#    define MGA_TDS_COLOR_ALPHA_PREVSTAGE       (0x00000010)
#    define MGA_TDS_COLOR_ARG1_REPLICATEALPHA   (0x00000020)
#    define MGA_TDS_COLOR_ARG1_INV              (0x00000040)
#    define MGA_TDS_COLOR_ARG2_REPLICATEALPHA   (0x00000080)
#    define MGA_TDS_COLOR_ARG2_INV              (0x00000100)
#    define MGA_TDS_COLOR_ALPHA1INV             (0x00000200)
#    define MGA_TDS_COLOR_ALPHA2INV             (0x00000400)
#    define MGA_TDS_COLOR_ARG1MUL_ALPHA1        (0x00000800)
#    define MGA_TDS_COLOR_ARG2MUL_ALPHA2        (0x00001000)
#    define MGA_TDS_COLOR_ARG1ADD_MULOUT        (0x00002000)
#    define MGA_TDS_COLOR_ARG2ADD_MULOUT        (0x00004000)
#    define MGA_TDS_COLOR_MODBRIGHT_2X          (0x00008000)
#    define MGA_TDS_COLOR_MODBRIGHT_4X          (0x00010000)
#    define MGA_TDS_COLOR_ADD_SUB               (0x00000000)
#    define MGA_TDS_COLOR_ADD_ADD               (0x00020000)
#    define MGA_TDS_COLOR_ADD2X                 (0x00040000)
#    define MGA_TDS_COLOR_ADDBIAS               (0x00080000)
#    define MGA_TDS_COLOR_BLEND                 (0x00100000)
#    define MGA_TDS_COLOR_SEL_ARG1              (0x00000000)
#    define MGA_TDS_COLOR_SEL_ARG2              (0x00200000)
#    define MGA_TDS_COLOR_SEL_ADD               (0x00400000)
#    define MGA_TDS_COLOR_SEL_MUL               (0x00600000)
#    define MGA_TDS_ALPHA_ARG1_INV              (0x00800000)
#    define MGA_TDS_ALPHA_ARG2_DIFFUSE          (0x00000000)
#    define MGA_TDS_ALPHA_ARG2_FCOL             (0x01000000)
#    define MGA_TDS_ALPHA_ARG2_PREVTEX          (0x02000000)
#    define MGA_TDS_ALPHA_ARG2_PREVSTAGE        (0x03000000)
#    define MGA_TDS_ALPHA_ARG2_INV              (0x04000000)
#    define MGA_TDS_ALPHA_ADD                   (0x08000000)
#    define MGA_TDS_ALPHA_ADDBIAS               (0x10000000)
#    define MGA_TDS_ALPHA_ADD2X                 (0x20000000)
#    define MGA_TDS_ALPHA_SEL_ARG1              (0x00000000)
#    define MGA_TDS_ALPHA_SEL_ARG2              (0x40000000)
#    define MGA_TDS_ALPHA_SEL_ADD               (0x80000000)
#    define MGA_TDS_ALPHA_SEL_MUL               (0xc0000000)

#define MGAREG_DWGSYNC		0x2c4c

#define MGAREG_AGP_PLL		0x1e4c
#define MGA_AGP2XPLL_ENABLE		0x1
#define MGA_AGP2XPLL_DISABLE		0x0

#endif
