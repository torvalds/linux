#ifndef _RK31XX_LCDC_H_
#define _RK31XX_LCDC_H_

#include<linux/rk_fb.h>
#include<linux/io.h>
#include<linux/clk.h>

enum _VOP_SOC_TYPE {
        VOP_RK3036 = 0,
        VOP_RK312X,
};

#ifdef BIT
#undef BIT
#endif
#define BIT(x, bit) ((x) << (bit))

#ifdef BIT_MASK
#undef BIT_MASK
#endif
#define BIT_MASK(x, bit, mask) BIT((x) & (mask), bit)

/*******************register definition**********************/

#define SYS_CTRL                (0x00)
        #define m_WIN0_EN               BIT(1, 0)
        #define m_WIN1_EN		BIT(1, 1)
        #define m_HWC_EN		BIT(1, 2)
        #define m_WIN0_FORMAT		BIT(7, 3)
        #define m_WIN1_FORMAT		BIT(7, 6)
        #define m_HWC_LUT_EN		BIT(1, 9)
        #define m_HWC_SIZE		BIT(1, 10)
        #define m_DIRECT_PATH_EN        BIT(1, 11)      /* rk312x */
        #define m_DIRECT_PATH_LAYER     BIT(1, 12)      /* rk312x */
        #define m_TVE_MODE_SEL          BIT(1, 13)      /* rk312x */
        #define m_TVE_DAC_EN            BIT(1, 14)      /* rk312x */
        #define m_WIN0_RB_SWAP		BIT(1, 15)
        #define m_WIN0_ALPHA_SWAP	BIT(1, 16)
        #define m_WIN0_Y8_SWAP		BIT(1, 17)
        #define m_WIN0_UV_SWAP		BIT(1, 18)
        #define m_WIN1_RB_SWAP		BIT(1, 19)
        #define m_WIN1_ALPHA_SWAP	BIT(1, 20)
        #define m_WIN1_ENDIAN_SWAP      BIT(1, 21)      /* rk312x */
        #define m_WIN0_OTSD_DISABLE	BIT(1, 22)
        #define m_WIN1_OTSD_DISABLE	BIT(1, 23)
        #define m_DMA_BURST_LENGTH	BIT(3, 24)
        #define m_HWC_LODAD_EN		BIT(1, 26)
        #define m_WIN1_LUT_EN           BIT(1, 27)      /* rk312x */
        #define m_DSP_LUT_EN            BIT(1, 28)      /* rk312x */
        #define m_DMA_STOP		BIT(1, 29)
        #define m_LCDC_STANDBY		BIT(1, 30)
        #define m_AUTO_GATING_EN	BIT(1, 31)
	
        #define v_WIN0_EN(x)		BIT_MASK(x, 1, 0)
        #define v_WIN1_EN(x)		BIT_MASK(x, 1, 1)
        #define v_HWC_EN(x)		BIT_MASK(x, 1, 2)
        #define v_WIN0_FORMAT(x)	BIT_MASK(x, 7, 3)
        #define v_WIN1_FORMAT(x)	BIT_MASK(x, 7, 6)
        #define v_HWC_LUT_EN(x)		BIT_MASK(x, 1, 9)
        #define v_HWC_SIZE(x)		BIT_MASK(x, 1, 10)
        #define v_DIRECT_PATH_EN(x)     BIT_MASK(x, 1, 11)
        #define v_DIRECT_PATH_LAYER(x)  BIT_MASK(x, 1, 12)
        #define v_TVE_MODE_SEL(x)       BIT_MASK(x, 1, 13)
        #define v_TVE_DAC_EN(x)         BIT_MASK(x, 1, 14)
        #define v_WIN0_RB_SWAP(x)	BIT_MASK(x, 1, 15)
        #define v_WIN0_ALPHA_SWAP(x)	BIT_MASK(x, 1, 16)
        #define v_WIN0_Y8_SWAP(x)	BIT_MASK(x, 1, 17)
        #define v_WIN0_UV_SWAP(x)	BIT_MASK(x, 1, 18)
        #define v_WIN1_RB_SWAP(x)	BIT_MASK(x, 1, 19)
        #define v_WIN1_ALPHA_SWAP(x)	BIT_MASK(x, 1, 20)
        #define v_WIN1_ENDIAN_SWAP(x)   BIT_MASK(x, 1, 21)
        #define v_WIN0_OTSD_DISABLE(x)	BIT_MASK(x, 1, 22)
        #define v_WIN1_OTSD_DISABLE(x)	BIT_MASK(x, 1, 23)
        #define v_DMA_BURST_LENGTH(x)	BIT_MASK(x, 3, 24)
        #define v_HWC_LODAD_EN(x)	BIT_MASK(x, 1, 26)
        #define v_WIN1_LUT_EN(x)	BIT_MASK(x, 1, 27)
        #define v_DSP_LUT_EN(x)         BIT_MASK(x, 1, 28)
        #define v_DMA_STOP(x)		BIT_MASK(x, 1, 29)
        #define v_LCDC_STANDBY(x)	BIT_MASK(x, 1, 30)
        #define v_AUTO_GATING_EN(x)	BIT_MASK(x, 1, 31)

#define DSP_CTRL0		(0x04)
        #define m_DSP_OUT_FORMAT	BIT(0x0f, 0)
        #define m_HSYNC_POL		BIT(1, 4)
        #define m_VSYNC_POL		BIT(1, 5)
        #define m_DEN_POL		BIT(1, 6)
        #define m_DCLK_POL		BIT(1, 7)
        #define m_WIN0_TOP		BIT(1, 8)
        #define m_DITHER_UP_EN		BIT(1, 9)
        #define m_DITHER_DOWN_MODE	BIT(1, 10)	/* use for rk312x */
        #define m_DITHER_DOWN_EN	BIT(1, 11)	/* use for rk312x */
        #define m_INTERLACE_DSP_EN	BIT(1, 12)
        #define m_INTERLACE_FIELD_POL	BIT(1, 13)	/* use for rk312x */
        #define m_WIN0_INTERLACE_EN	BIT(1, 14)	/* use for rk312x */
        #define m_WIN1_INTERLACE_EN	BIT(1, 15)
        #define m_WIN0_YRGB_DEFLICK_EN	BIT(1, 16)
        #define m_WIN0_CBR_DEFLICK_EN	BIT(1, 17)
        #define m_WIN0_ALPHA_MODE	BIT(1, 18)
        #define m_WIN1_ALPHA_MODE	BIT(1, 19)
        #define m_WIN0_CSC_MODE		BIT(3, 20)
        #define m_WIN0_YUV_CLIP		BIT(1, 23)
        #define m_TVE_MODE		BIT(1, 25)
        #define m_SW_UV_OFFSET_EN	BIT(1, 26)	/* use for rk312x */
        #define m_DITHER_DOWN_SEL	BIT(1, 27)	/* use for rk312x */
        #define m_HWC_ALPHA_MODE	BIT(1, 28)
        #define m_ALPHA_MODE_SEL0       BIT(1, 29)
        #define m_ALPHA_MODE_SEL1	BIT(1, 30)
        #define m_WIN1_DIFF_DCLK_EN	BIT(1, 31)	/* use for rk3036 */
        #define m_SW_OVERLAY_MODE	BIT(1, 31)	/* use for rk312x */
	
        #define v_DSP_OUT_FORMAT(x)	BIT_MASK(x, 0x0f, 0)
        #define v_HSYNC_POL(x)		BIT_MASK(x, 1, 4)
        #define v_VSYNC_POL(x)		BIT_MASK(x, 1, 5)
        #define v_DEN_POL(x)		BIT_MASK(x, 1, 6)
        #define v_DCLK_POL(x)		BIT_MASK(x, 1, 7)
        #define v_WIN0_TOP(x)		BIT_MASK(x, 1, 8)
        #define v_DITHER_UP_EN(x)	BIT_MASK(x, 1, 9)
        #define v_DITHER_DOWN_MODE(x)	BIT_MASK(x, 1, 10)	/* rk312x */
        #define v_DITHER_DOWN_EN(x)	BIT_MASK(x, 1, 11)	/* rk312x */
        #define v_INTERLACE_DSP_EN(x)	BIT_MASK(x, 1, 12)
        #define v_INTERLACE_FIELD_POL(x)	BIT_MASK(x, 1, 13)	/* rk312x */
        #define v_WIN0_INTERLACE_EN(x)		BIT_MASK(x, 1, 14)	/* rk312x */
        #define v_WIN1_INTERLACE_EN(x)		BIT_MASK(x, 1, 15)
        #define v_WIN0_YRGB_DEFLICK_EN(x)	BIT_MASK(x, 1, 16)
        #define v_WIN0_CBR_DEFLICK_EN(x)	BIT_MASK(x, 1, 17)
        #define v_WIN0_ALPHA_MODE(x)		BIT_MASK(x, 1, 18)
        #define v_WIN1_ALPHA_MODE(x)		BIT_MASK(x, 1, 19)
        #define v_WIN0_CSC_MODE(x)		BIT_MASK(x, 3, 20)
        #define v_WIN0_YUV_CLIP(x)		BIT_MASK(x, 1, 23)
        #define v_TVE_MODE(x)			BIT_MASK(x, 1, 25)
        #define v_SW_UV_OFFSET_EN(x)		BIT_MASK(x, 1, 26)      /* rk312x */
        #define v_DITHER_DOWN_SEL(x)		BIT_MASK(x, 1, 27)      /* rk312x */
        #define v_HWC_ALPHA_MODE(x)		BIT_MASK(x, 1, 28)
        #define v_ALPHA_MODE_SEL0(x)            BIT_MASK(x, 1, 29)
        #define v_ALPHA_MODE_SEL1(x)		BIT_MASK(x, 1, 30)
        #define v_WIN1_DIFF_DCLK_EN(x)		BIT_MASK(x, 1, 31)	/* rk3036 */
        #define v_SW_OVERLAY_MODE(x)		BIT_MASK(x, 1, 31)	/* rk312x */

#define DSP_CTRL1		(0x08)
        #define m_BG_COLOR		BIT(0xffffff, 0)
        #define m_BG_B			BIT(0xff, 0)
        #define m_BG_G			BIT(0xff, 8)
        #define m_BG_R			BIT(0xff, 16)
        #define m_BLANK_EN		BIT(1, 24)
        #define m_BLACK_EN		BIT(1, 25)
        #define m_DSP_BG_SWAP		BIT(1, 26)
        #define m_DSP_RB_SWAP		BIT(1, 27)
        #define m_DSP_RG_SWAP		BIT(1, 28)
        #define m_DSP_DELTA_SWAP	BIT(1, 29)              /* rk3036 */
        #define m_DSP_DUMMY_SWAP	BIT(1, 30)	        /* rk3036 */
        #define m_DSP_OUT_ZERO		BIT(1, 31)
	
        #define v_BG_COLOR(x)		BIT((x) & 0xffffff, 0)
        #define v_BG_B(x)		BIT((x) & 0xff, 0)
        #define v_BG_G(x)		BIT((x) & 0xff, 8)
        #define v_BG_R(x)		BIT((x) & 0xff, 16)
        #define v_BLANK_EN(x)		BIT_MASK(x, 1, 24)
        #define v_BLACK_EN(x)		BIT_MASK(x, 1, 25)
        #define v_DSP_BG_SWAP(x)	BIT_MASK(x, 1, 26)
        #define v_DSP_RB_SWAP(x)	BIT_MASK(x, 1, 27)
        #define v_DSP_RG_SWAP(x)	BIT_MASK(x, 1, 28)
        #define v_DSP_DELTA_SWAP(x)	BIT_MASK(x, 1, 29)      /* rk3036 */
        #define v_DSP_DUMMY_SWAP(x)	BIT_MASK(x, 1, 30)      /* rk3036 */
        #define v_DSP_OUT_ZERO(x)	BIT_MASK(x, 1, 31)

#define INT_SCALER              (0x0c)          /* only use for rk312x */
        #define m_SCALER_EMPTY_INTR_EN  BIT(1, 0)
        #define m_SCLAER_EMPTY_INTR_CLR BIT(1, 1)
        #define m_SCLAER_EMPTY_INTR_STA BIT(1, 2)
        #define m_FS_MASK_EN            BIT(1, 3)
        #define m_HDMI_HSYNC_POL        BIT(1, 4)
        #define m_HDMI_VSYNC_POL        BIT(1, 5)
        #define m_HDMI_DEN_POL          BIT(1, 6)

        #define v_SCALER_EMPTY_INTR_EN(x)       BIT_MASK(x, 1, 0)
        #define v_SCLAER_EMPTY_INTR_CLR(x)      BIT_MASK(x, 1, 1)
        #define v_SCLAER_EMPTY_INTR_STA(x)      BIT_MASK(x, 1, 2)
        #define v_FS_MASK_EN(x)                 BIT_MASK(x, 1, 3)
        #define v_HDMI_HSYNC_POL(x)             BIT_MASK(x, 1, 4)
        #define v_HDMI_VSYNC_POL(x)             BIT_MASK(x, 1, 5)
        #define v_HDMI_DEN_POL(x)               BIT_MASK(x. 1, 6)

#define INT_STATUS		(0x10)
        #define m_HS_INT_STA		BIT(1, 0)
        #define m_FS_INT_STA		BIT(1, 1)
        #define m_LF_INT_STA		BIT(1, 2)
        #define m_BUS_ERR_INT_STA	BIT(1, 3)
        #define m_HS_INT_EN		BIT(1, 4)
        #define m_FS_INT_EN          	BIT(1, 5)
        #define m_LF_INT_EN         	BIT(1, 6)
        #define m_BUS_ERR_INT_EN	BIT(1, 7)
        #define m_HS_INT_CLEAR		BIT(1, 8)
        #define m_FS_INT_CLEAR		BIT(1, 9)
        #define m_LF_INT_CLEAR		BIT(1, 10)
        #define m_BUS_ERR_INT_CLEAR	BIT(1, 11)
        #define m_LF_INT_NUM		BIT(0xfff, 12)
        #define m_WIN0_EMPTY_INT_EN	BIT(1, 24)
        #define m_WIN1_EMPTY_INT_EN	BIT(1, 25)
        #define m_WIN0_EMPTY_INT_CLEAR	BIT(1, 26)
        #define m_WIN1_EMPTY_INT_CLEAR	BIT(1, 27)
        #define m_WIN0_EMPTY_INT_STA	BIT(1, 28)
        #define m_WIN1_EMPTY_INT_STA	BIT(1, 29)
        #define m_FS_RAW_STA		BIT(1, 30)
        #define m_LF_RAW_STA		BIT(1, 31)
	
        #define v_HS_INT_EN(x)		BIT_MASK(x, 1, 4)
        #define v_FS_INT_EN(x)		BIT_MASK(x, 1, 5)
        #define v_LF_INT_EN(x)		BIT_MASK(x, 1, 6)
        #define v_BUS_ERR_INT_EN(x)	BIT_MASK(x, 1, 7)
        #define v_HS_INT_CLEAR(x)	BIT_MASK(x, 1, 8)
        #define v_FS_INT_CLEAR(x)	BIT_MASK(x, 1, 9)
        #define v_LF_INT_CLEAR(x)	BIT_MASK(x, 1, 10)
        #define v_BUS_ERR_INT_CLEAR(x)	BIT_MASK(x, 1, 11)
        #define v_LF_INT_NUM(x)		BIT((x) & 0xfff, 12)
        #define v_WIN0_EMPTY_INT_EN(x)	BIT_MASK(x, 1, 24)
        #define v_WIN1_EMPTY_INT_EN(x)	BIT_MASK(x, 1, 25)
        #define v_WIN0_EMPTY_INT_CLEAR(x)	BIT_MASK(x, 1, 26)
        #define v_WIN1_EMPTY_INT_CLEAR(x)	BIT_MASK(x, 1, 27)

#define ALPHA_CTRL		(0x14)
        #define m_WIN0_ALPHA_EN		BIT(1, 0)
        #define m_WIN1_ALPHA_EN		BIT(1, 1)
        #define m_HWC_ALPAH_EN		BIT(1, 2)
        #define m_WIN1_PREMUL_SCALE	BIT(1, 3)               /* rk3036 */
        #define m_WIN0_ALPHA_VAL	BIT(0xff, 4)
        #define m_WIN1_ALPHA_VAL	BIT(0xff, 12)
        #define m_HWC_ALPAH_VAL		BIT(0xff, 20)
	
        #define v_WIN0_ALPHA_EN(x)	BIT_MASK(x, 1, 0)
        #define v_WIN1_ALPHA_EN(x)	BIT_MASK(x, 1, 1)
        #define v_HWC_ALPAH_EN(x)	BIT_MASK(x, 1, 2)
        #define v_WIN1_PREMUL_SCALE(x)	BIT_MASK(x, 1, 3)       /* rk3036 */
        #define v_WIN0_ALPHA_VAL(x)	BIT((x) & 0xff, 4)
        #define v_WIN1_ALPHA_VAL(x)	BIT((x) & 0xff, 12)
        #define v_HWC_ALPAH_VAL(x)	BIT((x) & 0xff, 20)

#define WIN0_COLOR_KEY		(0x18)
#define WIN1_COLOR_KEY		(0x1c)
        #define m_COLOR_KEY_VAL		BIT(0xffffff, 0)
        #define m_COLOR_KEY_EN		BIT(1, 24)

        #define v_COLOR_KEY_VAL(x)	(((x) & 0xffffff) << 0)
        #define v_COLOR_KEY_EN(x)	BIT_MASK(x, 1, 24)

/* Layer Registers */
#define WIN0_YRGB_MST		(0x20)
#define WIN0_CBR_MST		(0x24)
#define WIN1_MST		(0xa0)                  /* rk3036 */
#define WIN1_MST_RK312X         (0x4c)                  /* rk312x */
#define HWC_MST			(0x58)

#define WIN1_VIR		(0x28)
#define WIN0_VIR		(0x30)
        #define m_YRGB_VIR	        BIT(0x1fff, 0)
        #define m_CBBR_VIR	        BIT(0x1fff, 16)   
	
        #define v_YRGB_VIR(x)           (((x) & 0x1fff) << 0)
        #define v_CBCR_VIR(x)           (((x) & 0x1fff) << 16)
	
	#define v_ARGB888_VIRWIDTH(x)	(((x) & 0x1fff) << 0)
	#define v_RGB888_VIRWIDTH(x) 	((((x*3)>>2)+((x)%3)) & 0x1fff)
	#define v_RGB565_VIRWIDTH(x)	(DIV_ROUND_UP(x, 2) & 0x1fff)
	#define v_YUV_VIRWIDTH(x)	(DIV_ROUND_UP(x, 4) & 0x1fff)

#define WIN0_ACT_INFO		(0x34)
#define WIN1_ACT_INFO		(0xb4)          /* rk3036 */
	#define m_ACT_WIDTH       	BIT(0x1fff, 0)
	#define m_ACT_HEIGHT      	BIT(0x1fff, 16)
 
	#define v_ACT_WIDTH(x)       	(((x - 1) & 0x1fff) << 0)
	#define v_ACT_HEIGHT(x)      	(((x - 1) & 0x1fff) << 16)

#define WIN0_DSP_INFO		(0x38)
#define WIN1_DSP_INFO		(0xb8)          /* rk3036 */
#define WIN1_DSP_INFO_RK312X    (0x50)          /* rk312x */
        #define m_DSP_WIDTH       	BIT(0x7ff, 0)
	#define m_DSP_HEIGHT      	BIT(0x7ff, 16)

	#define v_DSP_WIDTH(x)     	(((x - 1) & 0x7ff) << 0)
	#define v_DSP_HEIGHT(x)    	(((x - 1) & 0x7ff) << 16)
	
#define WIN0_DSP_ST		(0x3c)
#define WIN1_DSP_ST		(0xbc)          /* rk3036 */
#define WIN1_DSP_ST_RK312X      (0x54)          /* rk312x */
#define HWC_DSP_ST		(0x5c)
        #define m_DSP_STX               BIT(0xfff, 0)
	#define m_DSP_STY               BIT(0xfff, 16)

	#define v_DSP_STX(x)      	(((x) & 0xfff) << 0)
	#define v_DSP_STY(x)      	(((x) & 0xfff) << 16)
	
#define WIN0_SCL_FACTOR_YRGB	(0x40)
#define WIN0_SCL_FACTOR_CBR	(0x44)
#define WIN1_SCL_FACTOR_YRGB	(0xc0)          /* rk3036 */
        #define m_X_SCL_FACTOR          BIT(0xffff, 0)
	#define m_Y_SCL_FACTOR          BIT(0xffff, 16)

	#define v_X_SCL_FACTOR(x)  	(((x) & 0xffff) << 0)
	#define v_Y_SCL_FACTOR(x)  	(((x) & 0xffff) << 16)
	
#define WIN0_SCL_OFFSET		(0x48)
#define WIN1_SCL_OFFSET		(0xc8)          /* rk3036 */

/* LUT Registers */
#define WIN1_LUT_ADDR 		(0x0400)        /* rk3036 */
#define HWC_LUT_ADDR   		(0x0800)
#define DSP_LUT_ADDR            (0x0c00)        /* rk312x */

/* Display Infomation Registers */
#define DSP_HTOTAL_HS_END	(0x6c)
	#define v_HSYNC(x)  		(((x) & 0xfff) << 0)   /* hsync pulse width */
	#define v_HORPRD(x) 		(((x) & 0xfff) << 16)  /* horizontal period */

#define DSP_HACT_ST_END		(0x70)
	#define v_HAEP(x) 		(((x) & 0xfff) << 0)  /* horizontal active end point */
	#define v_HASP(x) 		(((x) & 0xfff) << 16) /* horizontal active start point */

#define DSP_VTOTAL_VS_END	(0x74)
	#define v_VSYNC(x) 		(((x) & 0xfff) << 0)
	#define v_VERPRD(x) 		(((x) & 0xfff) << 16)
	
#define DSP_VACT_ST_END		(0x78)
	#define v_VAEP(x) 		BIT((x) & 0xfff, 0)
	#define v_VASP(x) 		BIT((x) & 0xfff, 16)

#define DSP_VS_ST_END_F1	(0x7c)
	#define v_VSYNC_END_F1(x) 	(((x) & 0xfff) << 0)
	#define v_VSYNC_ST_F1(x) 	(((x) & 0xfff) << 16)
#define DSP_VACT_ST_END_F1	(0x80)
        #define v_VAEP_F1(x) 		(((x) & 0xfff) << 0)
	#define v_VASP_F1(x) 		(((x) & 0xfff) << 16)

/* Scaler Registers 
 * Only used for rk312x
 */
#define SCALER_CTRL             (0xa0)
        #define m_SCALER_EN             BIT(1, 0)
        #define m_SCALER_SYNC_INVERT    BIT(1, 2)
        #define m_SCALER_DEN_INVERT     BIT(1, 3)
        #define m_SCALER_OUT_ZERO       BIT(1, 4)
        #define m_SCALER_OUT_EN         BIT(1, 5)
        #define m_SCALER_VSYNC_MODE     BIT(3, 6)
        #define m_SCALER_VSYNC_VST      BIT(0xff, 8)

        #define v_SCALER_EN(x)          BIT_MASK(x, 1, 0)
        #define v_SCALER_SYNC_INVERT(x) BIT_MASK(x, 1, 2)
        #define v_SCALER_DEN_INVERT(x)  BIT_MASK(x, 1, 3)
        #define v_SCALER_OUT_ZERO(x)    BIT_MASK(x, 1, 4)
        #define v_SCALER_OUT_EN(x)      BIT_MASK(x, 1, 5)
        #define v_SCALER_VSYNC_MODE(x)  BIT_MASK(x, 3, 6)
        #define v_SCALER_VSYNC_VST(x)   BIT((x) & 0xff, 8)

#define SCALER_FACTOR           (0xa4)
        #define m_SCALER_H_FACTOR       BIT(0x3fff, 0)
        #define m_SCALER_V_FACTOR       BIT(0x3fff, 16)

        #define v_SCALER_H_FACTOR(x)    BIT((x) & 0x3fff, 0)
        #define v_SCALER_V_FACTOR(x)    BIT((x) & 0x3fff, 16)

#define SCALER_FRAME_ST         (0xa8)
        #define m_SCALER_FRAME_HST      BIT(0xfff, 0)
        #define m_SCALER_FRAME_VST      BIT(0xfff, 16)

        #define v_SCALER_FRAME_HST(x)   BIT((x) & 0xfff, 0)
        #define v_SCALER_FRAME_VST(x)   BIT((x) & 0xfff, 16)

#define SCALER_DSP_HOR_TIMING   (0xac)
        #define m_SCALER_HTOTAL         BIT(0xfff, 0)
        #define m_SCALER_HS_END         BIT(0xff, 16)

        #define v_SCALER_HTOTAL(x)      BIT((x) & 0xfff, 0)
        #define v_SCALER_HS_END(x)      BIT((x) & 0xff, 16)

#define SCALER_DSP_HACT_ST_END  (0xb0)
        #define m_SCALER_HAEP           BIT(0xfff, 0)
        #define m_SCALER_HASP           BIT(0x3ff, 16)

        #define v_SCALER_HAEP(x)        BIT((x) & 0xfff, 0)
        #define v_SCALER_HASP(x)        BIT((x) & 0x3ff, 16)

#define SCALER_DSP_VER_TIMING   (0xb4)
        #define m_SCALER_VTOTAL         BIT(0xfff, 0)
        #define m_SCALER_VS_END         BIT(0xff, 16)

        #define v_SCALER_VTOTAL(x)      BIT((x) & 0xfff, 0)
        #define v_SCALER_VS_END(x)      BIT((x) & 0xff, 16)

#define SCALER_DSP_VACT_ST_END  (0xb8)
        #define m_SCALER_VAEP           BIT(0xfff, 0)
        #define m_SCALER_VASP           BIT(0xff, 16)

        #define v_SCALER_VAEP(x)        BIT((x) & 0xfff, 0)
        #define v_SCALER_VASP(x)        BIT((x) & 0xff, 16)

#define SCALER_DSP_HBOR_TIMING  (0xbc)
        #define m_SCALER_HBOR_END       BIT(0xfff, 0)
        #define m_SCALER_HBOR_ST        BIT(0x3ff, 16)

        #define v_SCALER_HBOR_END(x)    BIT((x) & 0xfff, 0)
        #define v_SCALER_HBOR_ST(x)     BIT((x) & 0x3ff, 16)

#define SCALER_DSP_VBOR_TIMING  (0xc0)
        #define m_SCALER_VBOR_END       BIT(0xfff, 0)
        #define m_SCALER_VBOR_ST        BIT(0xff, 16)

        #define v_SCALER_VBOR_END(x)    BIT((x) & 0xfff, 0)
        #define v_SCALER_VBOR_ST(x)     BIT((x) & 0xff, 16)        

/* BCSH Registers */
#define BCSH_CTRL		(0xd0)
	#define m_BCSH_EN		BIT(1, 0)
        #define m_BCSH_R2Y_CSC_MODE     BIT(1, 1)       /* rk312x */
	#define m_BCSH_OUT_MODE		BIT(3, 2)
	#define m_BCSH_Y2R_CSC_MODE     BIT(3, 4)
        #define m_BCSH_Y2R_EN           BIT(1, 6)       /* rk312x */
        #define m_BCSH_R2Y_EN           BIT(1, 7)       /* rk312x */
	
	#define v_BCSH_EN(x)		BIT_MASK(x, 1, 0)
        #define v_BCSH_R2Y_CSC_MODE(x)  BIT_MASK(x, 1, 1)       /* rk312x */
	#define v_BCSH_OUT_MODE(x)	BIT_MASK(x, 3, 2)
	#define v_BCSH_CSC_MODE(x)	BIT_MASK(x, 3, 4)
        #define v_BCSH_Y2R_EN(x)        BIT_MASK(x, 1, 6)       /* rk312x */
        #define v_BCSH_R2Y_EN(x)        BIT_MASK(x, 1, 7)       /* rk312x */

#define BCSH_COLOR_BAR 		(0xd4)
        #define m_BCSH_COLOR_BAR_Y      BIT(0xff, 0)
	#define m_BCSH_COLOR_BAR_U	BIT(0xff, 8)
	#define m_BCSH_COLOR_BAR_V	BIT(0xff, 16)

	#define v_BCSH_COLOR_BAR_Y(x)	BIT((x) & 0xff, 0)
	#define v_BCSH_COLOR_BAR_U(x)   BIT((x) & 0xff, 8)
	#define v_BCSH_COLOR_BAR_V(x)   BIT((x) & 0xff, 16)

#define BCSH_BCS 		(0xd8)	
	#define m_BCSH_BRIGHTNESS	BIT(0x1f, 0)	
	#define m_BCSH_CONTRAST		BIT(0xff, 8)
	#define m_BCSH_SAT_CON		BIT(0x1ff, 16)

	#define v_BCSH_BRIGHTNESS(x)	BIT_MASK(x, 0x1f, 0)	
	#define v_BCSH_CONTRAST(x)	BIT((x) & 0xff, 8)	
	#define v_BCSH_SAT_CON(x)       BIT((x) & 0x1ff, 16)			

#define BCSH_H 			(0xdc)	
	#define m_BCSH_SIN_HUE		BIT(0xff, 0)
	#define m_BCSH_COS_HUE		BIT(0xff, 16)

	#define v_BCSH_SIN_HUE(x)	BIT((x) & 0xff, 0)
	#define v_BCSH_COS_HUE(x)	BIT((x) & 0xff, 16)

#define FRC_LOWER01_0           (0xe0)
#define FRC_LOWER01_1           (0xe4)
#define FRC_LOWER10_0           (0xe8)
#define FRC_LOWER10_1           (0xec)
#define FRC_LOWER11_0           (0xf0)
#define FRC_LOWER11_1           (0xf4)

/* Bus Register */
#define AXI_BUS_CTRL		(0x2c)
	#define m_IO_PAD_CLK			BIT(1, 31)
	#define m_CORE_CLK_DIV_EN		BIT(1, 30)
        #define m_MIPI_DCLK_INVERT              BIT(1, 29)      /* rk312x */
        #define m_MIPI_DCLK_EN                  BIT(1, 28)      /* rk312x */
        #define m_LVDS_DCLK_INVERT              BIT(1, 27)      /* rk312x */
        #define m_LVDS_DCLK_EN                  BIT(1, 26)      /* rk312x */
        #define m_RGB_DCLK_INVERT               BIT(1, 25)      /* rk312x */
        #define m_RGB_DCLK_EN                   BIT(1, 24)      /* rk312x */
	#define m_HDMI_DCLK_INVERT		BIT(1, 23)
	#define m_HDMI_DCLK_EN			BIT(1, 22)
	#define m_TVE_DAC_DCLK_INVERT		BIT(1, 21)
	#define m_TVE_DAC_DCLK_EN		BIT(1, 20)
	#define m_HDMI_DCLK_DIV_EN		BIT(1, 19)
	#define m_AXI_OUTSTANDING_MAX_NUM	BIT(0x1f, 12)
	#define m_AXI_MAX_OUTSTANDING_EN	BIT(1, 11)
	#define m_MMU_EN			BIT(1, 10)
	#define m_NOC_HURRY_THRESHOLD		BIT(0xf, 6)
	#define m_NOC_HURRY_VALUE		BIT(3, 4)
	#define m_NOC_HURRY_EN			BIT(1, 3)
	#define m_NOC_QOS_VALUE			BIT(3, 1)
	#define m_NOC_QOS_EN			BIT(1, 0)
	
	#define v_IO_PAD_CLK(x)			BIT_MASK(x, 1, 31)
	#define v_CORE_CLK_DIV_EN(x)		BIT_MASK(x, 1, 30)
        #define v_MIPI_DCLK_INVERT(x)           BIT_MASK(x, 1, 29)
        #define v_MIPI_DCLK_EN(x)               BIT_MASK(x, 1, 28)
        #define v_LVDS_DCLK_INVERT(x)           BIT_MASK(x, 1, 27)
        #define v_LVDS_DCLK_EN(x)               BIT_MASK(x, 1, 26)
        #define v_RGB_DCLK_INVERT(x)            BIT_MASK(x, 1, 25)
        #define v_RGB_DCLK_EN(x)                BIT_MASK(x, 1, 24)
	#define v_HDMI_DCLK_INVERT(x)		BIT_MASK(x, 1, 23)
	#define v_HDMI_DCLK_EN(x)		BIT_MASK(x, 1, 22)
	#define v_TVE_DAC_DCLK_INVERT(x)	BIT_MASK(x, 1, 21)
	#define v_TVE_DAC_DCLK_EN(x)		BIT_MASK(x, 1, 20)
	#define v_HDMI_DCLK_DIV_EN(x)		BIT_MASK(x, 1, 19)
	#define v_AXI_OUTSTANDING_MAX_NUM(x)	BIT_MASK(x, 0x1f, 12)
	#define v_AXI_MAX_OUTSTANDING_EN(x)	BIT_MASK(x, 1, 11)
	#define v_MMU_EN(x)			BIT_MASK(x, 1, 10)
	#define v_NOC_HURRY_THRESHOLD(x)	BIT_MASK(x, 0xf, 6)
	#define v_NOC_HURRY_VALUE(x)		BIT_MASK(x, 3, 4)
	#define v_NOC_HURRY_EN(x)		BIT_MASK(x, 1, 3)
	#define v_NOC_QOS_VALUE(x)		BIT_MASK(x, 3, 1)
	#define v_NOC_QOS_EN(x)			BIT_MASK(x, 1, 0)
	
#define GATHER_TRANSFER		(0x84)
	#define m_WIN1_AXI_GATHER_NUM		BIT(0xf, 12)
	#define m_WIN0_CBCR_AXI_GATHER_NUM	BIT(0x7, 8)
	#define m_WIN0_YRGB_AXI_GATHER_NUM	BIT(0xf, 4)
	#define m_WIN1_AXI_GAHTER_EN		BIT(1, 2)
	#define m_WIN0_CBCR_AXI_GATHER_EN	BIT(1, 1)
	#define m_WIN0_YRGB_AXI_GATHER_EN	BIT(1, 0)
	
	#define v_WIN1_AXI_GATHER_NUM(x)	BIT_MASK(x, 0xf, 12)
	#define v_WIN0_CBCR_AXI_GATHER_NUM(x)	BIT_MASK(x, 0x7, 8)
	#define v_WIN0_YRGB_AXI_GATHER_NUM(x)	BIT_MASK(x, 0xf, 4)
	#define v_WIN1_AXI_GAHTER_EN(x)		BIT_MASK(x, 1, 2)
	#define v_WIN0_CBCR_AXI_GATHER_EN(x)	BIT_MASK(x, 1, 1)
	#define v_WIN0_YRGB_AXI_GATHER_EN(x)	BIT_MASK(x, 1, 0)
	
#define VERSION_INFO		(0x94)
	#define m_MAJOR		                BIT(0xff, 24)
	#define m_MINOR		                BIT(0xff, 16)
	#define m_BUILD		                BIT(0xffff, 0)
		
#define REG_CFG_DONE		(0x90)

/* TV Control Registers */
#define TV_CTRL			(0x200)	
#define TV_SYNC_TIMING		(0x204)
#define TV_ACT_TIMING		(0x208)
#define TV_ADJ_TIMING		(0x20c)
#define TV_FREQ_SC		(0x210)
#define TV_FILTER0		(0x214)
#define TV_FILTER1		(0x218)
#define TV_FILTER2		(0x21C)
#define TV_ACT_ST		(0x234)
#define TV_ROUTING		(0x238)
#define TV_SYNC_ADJUST		(0x250)
#define TV_STATUS		(0x254)
#define TV_RESET		(0x268)
#define TV_SATURATION		(0x278)
#define TV_BW_CTRL		(0x28C)
#define TV_BRIGHTNESS_CONTRAST	(0x290)


/* MMU registers */
#define MMU_DTE_ADDR		(0x0300)
        #define m_MMU_DTE_ADDR			BIT(0xffffffff, 0)
	#define v_MMU_DTE_ADDR(x)		((x) & 0xffffffff)

#define MMU_STATUS		(0x0304)
        #define m_PAGING_ENABLED		BIT(1, 0)
	#define m_PAGE_FAULT_ACTIVE		BIT(1, 1)
	#define m_STAIL_ACTIVE			BIT(1, 2)
	#define m_MMU_IDLE			BIT(1, 3)
	#define m_REPLAY_BUFFER_EMPTY		BIT(1, 4)
	#define m_PAGE_FAULT_IS_WRITE		BIT(1, 5)
	#define m_PAGE_FAULT_BUS_ID		BIT(0x1f, 6)

	#define v_PAGING_ENABLED(x)		BIT_MASK(x, 1, 0)
	#define v_PAGE_FAULT_ACTIVE(x)		BIT_MASK(x, 1, 1)
	#define v_STAIL_ACTIVE(x)		BIT_MASK(x, 1, 2)
	#define v_MMU_IDLE(x)			BIT_MASK(x, 1, 3)
	#define v_REPLAY_BUFFER_EMPTY(x)	BIT_MASK(x, 1, 4)
	#define v_PAGE_FAULT_IS_WRITE(x)	BIT_MASK(x, 1, 5)
	#define v_PAGE_FAULT_BUS_ID(x)		BIT_MASK(x, 0x1f, 6)
	
#define MMU_COMMAND		(0x0308)
        #define m_MMU_CMD			BIT(0x7, 0)
	#define v_MMU_CMD(x)			BIT_MASK(x, 0x7, 0)	

#define MMU_PAGE_FAULT_ADDR	(0x030c)
        #define m_PAGE_FAULT_ADDR		BIT(0xffffffff, 0)
	#define v_PAGE_FAULT_ADDR(x)		((x) & 0xffffffff)
	
#define MMU_ZAP_ONE_LINE	(0x0310)
        #define m_MMU_ZAP_ONE_LINE		BIT(0xffffffff, 0)
	#define v_MMU_ZAP_ONE_LINE(x)		((x) & 0xffffffff)

#define MMU_INT_RAWSTAT		(0x0314)
        #define m_PAGE_FAULT_RAWSTAT		BIT(1, 0)
	#define m_READ_BUS_ERROR_RAWSTAT	BIT(1, 1)
 
	#define v_PAGE_FAULT_RAWSTAT(x)		BIT(x, 1, 0)
	#define v_READ_BUS_ERROR_RAWSTAT(x)	BIT(x, 1, 1)
	
#define MMU_INT_CLEAR		(0x0318)
        #define m_PAGE_FAULT_CLEAR		BIT(1, 0)
	#define m_READ_BUS_ERROR_CLEAR		BIT(1, 1)

	#define v_PAGE_FAULT_CLEAR(x)		BIT(x, 1, 0)
	#define v_READ_BUS_ERROR_CLEAR(x)	BIT(x, 1, 1)
	
#define MMU_INT_MASK		(0x031c)
        #define m_PAGE_FAULT_MASK		BIT(1, 0)
	#define m_READ_BUS_ERROR_MASK		BIT(1, 1)

	#define v_PAGE_FAULT_MASK(x)		BIT(x, 1, 0)
	#define v_READ_BUS_ERROR_MASK(x)	BIT(x, 1, 1)
	
#define MMU_INT_STATUS		(0x0320)
        #define m_PAGE_FAULT_STATUS		BIT(1, 0)
	#define m_READ_BUS_ERROR_STATUS		BIT(1, 1)

	#define v_PAGE_FAULT_STATUS(x)		BIT(x, 1, 0)
	#define v_READ_BUS_ERROR_STATUS(x)	BIT(x, 1, 1)

#define MMU_AUTO_GATING		(0x0324)
        #define m_MMU_AUTO_GATING		BIT(1, 0)
	#define v_MMU_AUTO_GATING(x)		BIT(x, 1, 0)


enum _vop_dma_burst {
	DMA_BURST_16 = 0,
	DMA_BURST_8,
	DMA_BURST_4
};

enum _vop_format_e {
	VOP_FORMAT_ARGB888 = 0,
	VOP_FORMAT_RGB888,
	VOP_FORMAT_RGB565,
	VOP_FORMAT_YCBCR420 = 4,
	VOP_FORMAT_YCBCR422,
	VOP_FORMAT_YCBCR444
};

enum _vop_tv_mode {
	TV_NTSC,
	TV_PAL,
};

enum _vop_csc_mode {
	VOP_CSC_BT601 = 0,
	VOP_CSC_JPEG,
	VOP_CSC_BT709
};

enum _vop_hwc_size {
	VOP_HWC_SIZE_32,
	VOP_HWC_SIZE_64
};

#define CalScale(x, y)	             ((((u32)(x - 1)) * 0x1000) / (y - 1))

struct rk_lcdc_drvdata {
     u8 soc_type;
     u32 reserve;
};

struct lcdc_device {
	int id;
        u8 soc_type;
	struct rk_lcdc_driver driver;
	struct device *dev;
	struct rk_screen *screen;

	void __iomem *regs;
	void *regsbak;			/* back up reg */
	u32 reg_phy_base;       	/* physical basic address of lcdc register */
	u32 len;               		/* physical map length of lcdc register */
	spinlock_t  reg_lock;		/* one time only one process allowed to config the register */
	
	int __iomem *dsp_lut_addr_base;

	int prop;			/* used for primary or extended display device */
	bool pre_init;
	bool pwr18;			/* if lcdc use 1.8v power supply */
	bool clk_on;			/* if aclk or hclk is closed ,acess to register is not allowed */
	u8 atv_layer_cnt;		/* active layer counter,when atv_layer_cnt = 0,lcdc is disable*/

	unsigned int		irq;

	struct clk		*pd;	/* lcdc power domain */
	struct clk		*hclk;	/* lcdc AHP clk */
	struct clk		*dclk;	/* lcdc dclk */
	struct clk		*aclk;	/* lcdc share memory frequency */
	u32 pixclock;	

	u32 standby;			/* 1:standby,0:work */
};

static inline void lcdc_writel(struct lcdc_device *lcdc_dev, u32 offset, u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);	
	*_pv = v;
	writel_relaxed(v, lcdc_dev->regs + offset);	
}

static inline u32 lcdc_readl(struct lcdc_device *lcdc_dev, u32 offset)
{
	u32 v;
	u32 *_pv = (u32*)lcdc_dev->regsbak;
	_pv += (offset >> 2);
	v = readl_relaxed(lcdc_dev->regs + offset);
	*_pv = v;
	return v;
}

static inline u32 lcdc_read_bit(struct lcdc_device *lcdc_dev, u32 offset,
                                u32 msk) 
{
       u32 _v = readl_relaxed(lcdc_dev->regs + offset); 
       _v &= msk;
       return (_v? 1 : 0);   
}

static inline void  lcdc_set_bit(struct lcdc_device *lcdc_dev, u32 offset,
                                 u32 msk) 
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) |= msk;				
	writel_relaxed(*_pv, lcdc_dev->regs + offset); 
} 

static inline void lcdc_clr_bit(struct lcdc_device *lcdc_dev, u32 offset,
                                u32 msk)
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) &= (~msk);				
	writel_relaxed(*_pv, lcdc_dev->regs + offset); 
} 

static inline void  lcdc_msk_reg(struct lcdc_device *lcdc_dev, u32 offset,
                                 u32 msk, u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);			
	(*_pv) &= (~msk);				
	(*_pv) |= v;				
	writel_relaxed(*_pv, lcdc_dev->regs + offset);	
}

static inline void lcdc_cfg_done(struct lcdc_device *lcdc_dev) 
{
	writel_relaxed(0x01, lcdc_dev->regs + REG_CFG_DONE); 
	dsb();	
} 

#endif /* _RK31XX_LCDC_H_ */
