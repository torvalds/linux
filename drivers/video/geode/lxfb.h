#ifndef _LXFB_H_
#define _LXFB_H_

#include <linux/fb.h>

#define OUTPUT_CRT   0x01
#define OUTPUT_PANEL 0x02

struct lxfb_par {
	int output;
	int panel_width;
	int panel_height;

	void __iomem *gp_regs;
	void __iomem *dc_regs;
	void __iomem *df_regs;
};

static inline unsigned int lx_get_pitch(unsigned int xres, int bpp)
{
	return (((xres * (bpp >> 3)) + 7) & ~7);
}

void lx_set_mode(struct fb_info *);
void lx_get_gamma(struct fb_info *, unsigned int *, int);
void lx_set_gamma(struct fb_info *, unsigned int *, int);
unsigned int lx_framebuffer_size(void);
int lx_blank_display(struct fb_info *, int);
void lx_set_palette_reg(struct fb_info *, unsigned int, unsigned int,
			unsigned int, unsigned int);

/* MSRS */

#define MSR_LX_GLD_CONFIG    0x48002001
#define MSR_LX_GLCP_DOTPLL   0x4c000015
#define MSR_LX_DF_PADSEL     0x48002011
#define MSR_LX_DC_SPARE      0x80000011
#define MSR_LX_DF_GLCONFIG   0x48002001

#define MSR_LX_GLIU0_P2D_RO0 0x10000029

#define GLCP_DOTPLL_RESET    (1 << 0)
#define GLCP_DOTPLL_BYPASS   (1 << 15)
#define GLCP_DOTPLL_HALFPIX  (1 << 24)
#define GLCP_DOTPLL_LOCK     (1 << 25)

#define DF_CONFIG_OUTPUT_MASK       0x38
#define DF_OUTPUT_PANEL             0x08
#define DF_OUTPUT_CRT               0x00
#define DF_SIMULTANEOUS_CRT_AND_FP  (1 << 15)

#define DF_DEFAULT_TFT_PAD_SEL_LOW  0xDFFFFFFF
#define DF_DEFAULT_TFT_PAD_SEL_HIGH 0x0000003F

#define DC_SPARE_DISABLE_CFIFO_HGO         0x00000800
#define DC_SPARE_VFIFO_ARB_SELECT          0x00000400
#define DC_SPARE_WM_LPEN_OVRD              0x00000200
#define DC_SPARE_LOAD_WM_LPEN_MASK         0x00000100
#define DC_SPARE_DISABLE_INIT_VID_PRI      0x00000080
#define DC_SPARE_DISABLE_VFIFO_WM          0x00000040
#define DC_SPARE_DISABLE_CWD_CHECK         0x00000020
#define DC_SPARE_PIX8_PAN_FIX              0x00000010
#define DC_SPARE_FIRST_REQ_MASK            0x00000002

/* Registers */

#define DC_UNLOCK         0x00
#define  DC_UNLOCK_CODE   0x4758

#define DC_GENERAL_CFG    0x04
#define  DC_GCFG_DFLE     (1 << 0)
#define  DC_GCFG_VIDE     (1 << 3)
#define  DC_GCFG_VGAE     (1 << 7)
#define  DC_GCFG_CMPE     (1 << 5)
#define  DC_GCFG_DECE     (1 << 6)
#define  DC_GCFG_FDTY     (1 << 17)

#define DC_DISPLAY_CFG    0x08
#define  DC_DCFG_TGEN     (1 << 0)
#define  DC_DCFG_GDEN     (1 << 3)
#define  DC_DCFG_VDEN     (1 << 4)
#define  DC_DCFG_TRUP     (1 << 6)
#define  DC_DCFG_DCEN     (1 << 24)
#define  DC_DCFG_PALB     (1 << 25)
#define  DC_DCFG_VISL     (1 << 27)

#define  DC_DCFG_16BPP           0x0

#define  DC_DCFG_DISP_MODE_MASK  0x00000300
#define  DC_DCFG_DISP_MODE_8BPP  0x00000000
#define  DC_DCFG_DISP_MODE_16BPP 0x00000100
#define  DC_DCFG_DISP_MODE_24BPP 0x00000200
#define  DC_DCFG_DISP_MODE_32BPP 0x00000300


#define DC_ARB_CFG        0x0C

#define DC_FB_START       0x10
#define DC_CB_START       0x14
#define DC_CURSOR_START   0x18

#define DC_DV_TOP          0x2C
#define DC_DV_TOP_ENABLE   (1 << 0)

#define DC_LINE_SIZE       0x30
#define DC_GRAPHICS_PITCH  0x34
#define DC_H_ACTIVE_TIMING 0x40
#define DC_H_BLANK_TIMING  0x44
#define DC_H_SYNC_TIMING   0x48
#define DC_V_ACTIVE_TIMING 0x50
#define DC_V_BLANK_TIMING  0x54
#define DC_V_SYNC_TIMING   0x58
#define DC_FB_ACTIVE       0x5C

#define DC_PAL_ADDRESS     0x70
#define DC_PAL_DATA        0x74

#define DC_PHY_MEM_OFFSET  0x84

#define DC_DV_CTL          0x88
#define DC_DV_LINE_SIZE_MASK               0x00000C00
#define DC_DV_LINE_SIZE_1024               0x00000000
#define DC_DV_LINE_SIZE_2048               0x00000400
#define DC_DV_LINE_SIZE_4096               0x00000800
#define DC_DV_LINE_SIZE_8192               0x00000C00


#define DC_GFX_SCALE       0x90
#define DC_IRQ_FILT_CTL    0x94


#define DC_IRQ               0xC8
#define  DC_IRQ_MASK         (1 << 0)
#define  DC_VSYNC_IRQ_MASK   (1 << 1)
#define  DC_IRQ_STATUS       (1 << 20)
#define  DC_VSYNC_IRQ_STATUS (1 << 21)

#define DC_GENLCK_CTRL      0xD4
#define  DC_GENLCK_ENABLE   (1 << 18)
#define  DC_GC_ALPHA_FLICK_ENABLE  (1 << 25)
#define  DC_GC_FLICKER_FILTER_ENABLE (1 << 24)
#define  DC_GC_FLICKER_FILTER_MASK (0x0F << 28)

#define DC_COLOR_KEY       0xB8
#define DC_CLR_KEY_ENABLE (1 << 24)


#define DC3_DV_LINE_SIZE_MASK               0x00000C00
#define DC3_DV_LINE_SIZE_1024               0x00000000
#define DC3_DV_LINE_SIZE_2048               0x00000400
#define DC3_DV_LINE_SIZE_4096               0x00000800
#define DC3_DV_LINE_SIZE_8192               0x00000C00

#define DF_VIDEO_CFG       0x0
#define  DF_VCFG_VID_EN    (1 << 0)

#define DF_DISPLAY_CFG     0x08

#define DF_DCFG_CRT_EN     (1 << 0)
#define DF_DCFG_HSYNC_EN   (1 << 1)
#define DF_DCFG_VSYNC_EN   (1 << 2)
#define DF_DCFG_DAC_BL_EN  (1 << 3)
#define DF_DCFG_CRT_HSYNC_POL  (1 << 8)
#define DF_DCFG_CRT_VSYNC_POL  (1 << 9)
#define DF_DCFG_GV_PAL_BYP     (1 << 21)

#define DF_DCFG_CRT_SYNC_SKW_INIT 0x10000
#define DF_DCFG_CRT_SYNC_SKW_MASK  0x1c000

#define DF_DCFG_PWR_SEQ_DLY_INIT     0x80000
#define DF_DCFG_PWR_SEQ_DLY_MASK     0xe0000

#define DF_MISC            0x50

#define  DF_MISC_GAM_BYPASS (1 << 0)
#define  DF_MISC_DAC_PWRDN  (1 << 10)
#define  DF_MISC_A_PWRDN    (1 << 11)

#define DF_PAR             0x38
#define DF_PDR             0x40
#define DF_ALPHA_CONTROL_1 0xD8
#define DF_VIDEO_REQUEST   0x120

#define DF_PANEL_TIM1      0x400
#define DF_DEFAULT_TFT_PMTIM1 0x0

#define DF_PANEL_TIM2      0x408
#define DF_DEFAULT_TFT_PMTIM2 0x08000000

#define DF_FP_PM             0x410
#define  DF_FP_PM_P          (1 << 24)

#define DF_DITHER_CONTROL    0x418
#define DF_DEFAULT_TFT_DITHCTL                  0x00000070
#define GP_BLT_STATUS      0x44
#define  GP_BS_BLT_BUSY    (1 << 0)
#define  GP_BS_CB_EMPTY    (1 << 4)

#endif
