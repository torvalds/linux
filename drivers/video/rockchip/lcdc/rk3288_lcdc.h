#ifndef RK3288_LCDC_H_
#define RK3288_LCDC_H_

#include<linux/rk_fb.h>
#include<linux/io.h>
#include<linux/clk.h>


/*******************register definition**********************/

#define REG_CFG_DONE            	(0x0000)
#define VERSION_INFO            	(0x0004)
#define m_RTL_VERSION 			(0xffff<<0)
#define m_FPGA_VERSION			(0xffff<<16)
#define VOP_FULL_RK3288_V1_0		0x03007236
#define VOP_FULL_RK3288_V1_1		0x0a050a01
#define SYS_CTRL                	(0x0008)
#define v_DIRECT_PATH_EN(x)     	(((x)&1)<<0)
#define v_DIRECT_PATCH_SEL(x)   	(((x)&3)<<1)
#define v_DOUB_CHANNEL_EN(x)    	(((x)&1)<<3)
#define v_DOUB_CH_OVERLAP_NUM(x)        (((x)&0xf)<<4)
#define v_EDPI_HALT_EN(x)    		(((x)&1)<<8)
#define v_EDPI_WMS_MODE(x)              (((x)&1)<<9)
#define v_EDPI_WMS_FS(x)                (((x)&1)<<10)
#define v_HDMI_DCLK_OUT_EN(x)		(((x)&1)<<11)
#define v_RGB_OUT_EN(x)                 (((x)&1)<<12)
#define v_HDMI_OUT_EN(x)                (((x)&1)<<13)
#define v_EDP_OUT_EN(x)                 (((x)&1)<<14)
#define v_MIPI_OUT_EN(x)                (((x)&1)<<15)
#define v_DMA_BURST_LENGTH(x) 		(((x)&3)<<18)
#define v_MMU_EN(x)    	        	(((x)&1)<<20)
#define v_DMA_STOP(x)                   (((x)&1)<<21)
#define v_STANDBY_EN(x)      		(((x)&1)<<22)
#define v_AUTO_GATING_EN(x)   		(((x)&1)<<23)

#define m_DIRECT_PATH_EN     	        (1<<0)
#define m_DIRECT_PATCH_SEL  		(3<<1)
#define m_DOUB_CHANNEL_EN    		(1<<3)
#define m_DOUB_CH_OVERLAP_NUM           (0xf<<4)
#define m_EDPI_HALT_EN       		(1<<8)
#define m_EDPI_WMS_MODE                 (1<<9)
#define m_EDPI_WMS_FS                   (1<<10)
#define m_HDMI_DCLK_OUT_EN		(1<<11)
#define m_RGB_OUT_EN                    (1<<12)
#define m_HDMI_OUT_EN                   (1<<13)
#define m_EDP_OUT_EN                    (1<<14)
#define m_MIPI_OUT_EN                   (1<<15)
#define m_DMA_BURST_LENGTH    		(3<<18)
#define m_MMU_EN       	        	(1<<20)
#define m_DMA_STOP			(1<<21)
#define m_STANDBY_EN      		(1<<22)
#define m_AUTO_GATING_EN      		(1<<23)
#define SYS_CTRL1            		(0x000c)
#define v_NOC_HURRY_EN(x)               (((x)&0x1 )<<0 ) 
#define v_NOC_HURRY_VALUE(x)            (((x)&0x3 )<<1 )
#define v_NOC_HURRY_THRESHOLD(x)        (((x)&0x3f)<<3 )
#define v_NOC_QOS_EN(x)                 (((x)&0x1 )<<9 )
#define v_NOC_WIN_QOS(x)                (((x)&0x3 )<<10)
#define v_AXI_MAX_OUTSTANDING_EN(x)     (((x)&0x1 )<<12)
#define v_AXI_OUTSTANDING_MAX_NUM(x)    (((x)&0x1f)<<13)

#define m_NOC_HURRY_EN                  (0x1 <<0 )
#define m_NOC_HURRY_VALUE               (0x3 <<1 )
#define m_NOC_HURRY_THRESHOLD           (0x3f<<3 )
#define m_NOC_QOS_EN                    (0x1 <<9 )
#define m_NOC_WIN_QOS                   (0x3 <<10)
#define m_AXI_MAX_OUTSTANDING_EN        (0x1 <<12)
#define m_AXI_OUTSTANDING_MAX_NUM       (0x1f<<13)

#define DSP_CTRL0               	(0x0010)
#define v_DSP_OUT_MODE(x)       	(((x)&0x0f)<<0)
#define v_DSP_HSYNC_POL(x)      	(((x)&1)<<4)
#define v_DSP_VSYNC_POL(x)      	(((x)&1)<<5)
#define v_DSP_DEN_POL(x)        	(((x)&1)<<6)
#define v_DSP_DCLK_POL(x)       	(((x)&1)<<7)
#define v_DSP_DCLK_DDR(x)       	(((x)&1)<<8)
#define v_DSP_DDR_PHASE(x)      	(((x)&1)<<9)
#define v_DSP_INTERLACE(x)      	(((x)&1)<<10)
#define v_DSP_FIELD_POL(x)      	(((x)&1)<<11)
#define v_DSP_BG_SWAP(x)        	(((x)&1)<<12)
#define v_DSP_RB_SWAP(x)        	(((x)&1)<<13)
#define v_DSP_RG_SWAP(x)        	(((x)&1)<<14)
#define v_DSP_DELTA_SWAP(x)     	(((x)&1)<<15)
#define v_DSP_DUMMY_SWAP(x)     	(((x)&1)<<16)
#define v_DSP_OUT_ZERO(x)       	(((x)&1)<<17)
#define v_DSP_BLANK_EN(x)       	(((x)&1)<<18)
#define v_DSP_BLACK_EN(x)       	(((x)&1)<<19)
#define v_DSP_CCIR656_AVG(x)    	(((x)&1)<<20)
#define v_DSP_YUV_CLIP(x)       	(((x)&1)<<21)
#define v_DSP_X_MIR_EN(x)       	(((x)&1)<<22)
#define v_DSP_Y_MIR_EN(x)       	(((x)&1)<<23)
#define m_DSP_OUT_MODE       		(0x0f<<0)
#define m_DSP_HSYNC_POL       		(1<<4)
#define m_DSP_VSYNC_POL       		(1<<5)
#define m_DSP_DEN_POL       		(1<<6)
#define m_DSP_DCLK_POL        		(1<<7)
#define m_DSP_DCLK_DDR      		(1<<8)
#define m_DSP_DDR_PHASE      		(1<<9)
#define m_DSP_INTERLACE      		(1<<10)
#define m_DSP_FIELD_POL      		(1<<11)
#define m_DSP_BG_SWAP       		(1<<12)
#define m_DSP_RB_SWAP       		(1<<13)
#define m_DSP_RG_SWAP        		(1<<14)
#define m_DSP_DELTA_SWAP      		(1<<15)
#define m_DSP_DUMMY_SWAP       		(1<<16)
#define m_DSP_OUT_ZERO       		(1<<17)
#define m_DSP_BLANK_EN     		(1<<18)
#define m_DSP_BLACK_EN      		(1<<19)
#define m_DSP_CCIR656_AVG     		(1<<20)
#define m_DSP_YUV_CLIP      		(1<<21)
#define m_DSP_X_MIR_EN      		(1<<22)
#define m_DSP_Y_MIR_EN      		(1<<23)

#define DSP_CTRL1 			(0x0014)
#define v_DSP_LUT_EN(x)         	(((x)&1)<<0)
#define v_PRE_DITHER_DOWN_EN(x) 	(((x)&1)<<1)
#define v_DITHER_DOWN_EN(x)     	(((x)&1)<<2)
#define v_DITHER_DOWN_MODE(x)   	(((x)&1)<<3)
#define v_DITHER_DOWN_SEL(x)    	(((x)&1)<<4)
#define v_DITHER_UP_EN(x)		(((x)&1)<<6)
#define v_DSP_LAYER0_SEL(x)		(((x)&3)<<8)
#define v_DSP_LAYER1_SEL(x)		(((x)&3)<<10)
#define v_DSP_LAYER2_SEL(x)		(((x)&3)<<12)
#define v_DSP_LAYER3_SEL(x)		(((x)&3)<<14)
#define m_DSP_LUT_EN          		(1<<0)
#define m_PRE_DITHER_DOWN_EN  		(1<<1)
#define m_DITHER_DOWN_EN      		(1<<2)
#define m_DITHER_DOWN_MODE   		(1<<3)
#define m_DITHER_DOWN_SEL    		(1<<4)
#define m_DITHER_UP_EN			(1<<6)
#define m_DSP_LAYER0_SEL		(3<<8)
#define m_DSP_LAYER1_SEL		(3<<10)
#define m_DSP_LAYER2_SEL		(3<<12)
#define m_DSP_LAYER3_SEL		(3<<14)

#define DSP_BG 				(0x0018)
#define v_DSP_BG_BLUE(x)        	(((x<<2)&0x3ff)<<0)
#define v_DSP_BG_GREEN(x)       	(((x<<2)&0x3ff)<<10)
#define v_DSP_BG_RED(x)         	(((x<<2)&0x3ff)<<20)
#define m_DSP_BG_BLUE        		(0x3ff<<0)
#define m_DSP_BG_GREEN      		(0x3ff<<10)
#define m_DSP_BG_RED        		(0x3ff<<20)

#define MCU_CTRL 	        	(0x001c)
#define v_MCU_PIX_TOTAL(x)      	(((x)&0x3f)<<0)
#define v_MCU_CS_PST(x)         	(((x)&0xf)<<6)
#define v_MCU_CS_PEND(x)        	(((x)&0x3f)<<10)
#define v_MCU_RW_PST(x)         	(((x)&0xf)<<16)
#define v_MCU_RW_PEND(x)        	(((x)&0x3f)<<20)
#define v_MCU_CLK_SEL(x)        	(((x)&1)<<26)   
#define v_MCU_HOLD_MODE(x)      	(((x)&1)<<27)
#define v_MCU_FRAME_ST(x)       	(((x)&1)<<28)
#define v_MCU_RS(x)         		(((x)&1)<<29)
#define v_MCU_BYPASS(x)         	(((x)&1)<<30)
#define v_MCU_TYPE(x)            	(((x)&1)<<31)
#define m_MCU_PIX_TOTAL       		(0x3f<<0)
#define m_MCU_CS_PST          		(0xf<<6)
#define m_MCU_CS_PEND        		(0x3f<<10)
#define m_MCU_RW_PST          		(0xf<<16)
#define m_MCU_RW_PEND         		(0x3f<<20)
#define m_MCU_CLK_SEL         		(1<<26)   
#define m_MCU_HOLD_MODE       		(1<<27)
#define m_MCU_FRAME_ST        		(1<<28)
#define m_MCU_RS          		(1<<29)
#define m_MCU_BYPASS          		(1<<30)
#define m_MCU_TYPE           		((u32)1<<31)

#define INTR_CTRL0 			(0x0020)
#define v_DSP_HOLD_VALID_INTR_STS(x)    (((x)&1)<<0)
#define v_FS_INTR_STS(x)        	(((x)&1)<<1)
#define v_LINE_FLAG_INTR_STS(x) 	(((x)&1)<<2)
#define v_BUS_ERROR_INTR_STS(x) 	(((x)&1)<<3)
#define v_DSP_HOLD_VALID_INTR_EN(x)  	(((x)&1)<<4)
#define v_FS_INTR_EN(x)         	(((x)&1)<<5)
#define v_LINE_FLAG_INTR_EN(x)  	(((x)&1)<<6)
#define v_BUS_ERROR_INTR_EN(x)        	(((x)&1)<<7)
#define v_DSP_HOLD_VALID_INTR_CLR(x)    (((x)&1)<<8)
#define v_FS_INTR_CLR(x)        	(((x)&1)<<9)
#define v_LINE_FLAG_INTR_CLR(x)        	(((x)&1)<<10)
#define v_BUS_ERROR_INTR_CLR(x)        	(((x)&1)<<11)
#define v_DSP_LINE_FLAG_NUM(x)        	(((x)&0xfff)<<12)

#define m_DSP_HOLD_VALID_INTR_STS     	(1<<0)
#define m_FS_INTR_STS         		(1<<1)
#define m_LINE_FLAG_INTR_STS  		(1<<2)
#define m_BUS_ERROR_INTR_STS 		(1<<3)
#define m_DSP_HOLD_VALID_INTR_EN   	(1<<4)
#define m_FS_INTR_EN         		(1<<5)
#define m_LINE_FLAG_INTR_EN 		(1<<6)
#define m_BUS_ERROR_INTR_EN         	(1<<7)
#define m_DSP_HOLD_VALID_INTR_CLR     	(1<<8)
#define m_FS_INTR_CLR       		(1<<9)
#define m_LINE_FLAG_INTR_CLR         	(1<<10)
#define m_BUS_ERROR_INTR_CLR         	(1<<11)
#define m_DSP_LINE_FLAG_NUM         	(0xfff<<12)

#define INTR_CTRL1 			(0x0024)
#define v_WIN0_EMPTY_INTR_STS(x)	(((x)&1)<<0)
#define v_WIN1_EMPTY_INTR_STS(x)	(((x)&1)<<1)
#define v_WIN2_EMPTY_INTR_STS(x)	(((x)&1)<<2)
#define v_WIN3_EMPTY_INTR_STS(x)	(((x)&1)<<3)
#define v_HWC_EMPTY_INTR_STS(x)		(((x)&1)<<4)
#define v_POST_BUF_EMPTY_INTR_STS(x)	(((x)&1)<<5)
#define v_PWM_GEN_INTR_STS(x)		(((x)&1)<<6)
#define v_WIN0_EMPTY_INTR_EN(x)		(((x)&1)<<8)
#define v_WIN1_EMPTY_INTR_EN(x)		(((x)&1)<<9)
#define v_WIN2_EMPTY_INTR_EN(x)		(((x)&1)<<10)
#define v_WIN3_EMPTY_INTR_EN(x)		(((x)&1)<<11)
#define v_HWC_EMPTY_INTR_EN(x)		(((x)&1)<<12)
#define v_POST_BUF_EMPTY_INTR_EN(x)	(((x)&1)<<13)
#define v_PWM_GEN_INTR_EN(x)		(((x)&1)<<14)
#define v_WIN0_EMPTY_INTR_CLR(x)	(((x)&1)<<16)
#define v_WIN1_EMPTY_INTR_CLR(x)	(((x)&1)<<17)
#define v_WIN2_EMPTY_INTR_CLR(x)	(((x)&1)<<18)
#define v_WIN3_EMPTY_INTR_CLR(x)	(((x)&1)<<19)
#define v_HWC_EMPTY_INTR_CLR(x)		(((x)&1)<<20)
#define v_POST_BUF_EMPTY_INTR_CLR(x)	(((x)&1)<<21)
#define v_PWM_GEN_INTR_CLR(x)		(((x)&1)<<22)

#define m_WIN0_EMPTY_INTR_STS 		(1<<0)
#define m_WIN1_EMPTY_INTR_STS 		(1<<1)
#define m_WIN2_EMPTY_INTR_STS 		(1<<2)
#define m_WIN3_EMPTY_INTR_STS 		(1<<3)
#define m_HWC_EMPTY_INTR_STS 		(1<<4)
#define m_POST_BUF_EMPTY_INTR_STS 	(1<<5)
#define m_PWM_GEN_INTR_STS 		(1<<6)
#define m_WIN0_EMPTY_INTR_EN 		(1<<8)
#define m_WIN1_EMPTY_INTR_EN 		(1<<9)
#define m_WIN2_EMPTY_INTR_EN 		(1<<10)
#define m_WIN3_EMPTY_INTR_EN 		(1<<11)
#define m_HWC_EMPTY_INTR_EN 		(1<<12)
#define m_POST_BUF_EMPTY_INTR_EN 	(1<<13)
#define m_PWM_GEN_INTR_EN 		(1<<14)
#define m_WIN0_EMPTY_INTR_CLR 		(1<<16)
#define m_WIN1_EMPTY_INTR_CLR 		(1<<17)
#define m_WIN2_EMPTY_INTR_CLR 		(1<<18)
#define m_WIN3_EMPTY_INTR_CLR 		(1<<19)
#define m_HWC_EMPTY_INTR_CLR 		(1<<20)
#define m_POST_BUF_EMPTY_INTR_CLR 	(1<<21)
#define m_PWM_GEN_INTR_CLR 		(1<<22)

/*win0 register*/
#define WIN0_CTRL0 			(0x0030)
#define v_WIN0_EN(x)			(((x)&1)<<0)
#define v_WIN0_DATA_FMT(x)		(((x)&7)<<1)
#define v_WIN0_FMT_10(x)		(((x)&1)<<4)
#define v_WIN0_LB_MODE(x)		(((x)&7)<<5)
#define v_WIN0_INTERLACE_READ(x)	(((x)&1)<<8)
#define v_WIN0_NO_OUTSTANDING(x)	(((x)&1)<<9)
#define v_WIN0_CSC_MODE(x)		(((x)&3)<<10)
#define v_WIN0_RB_SWAP(x)		(((x)&1)<<12)
#define v_WIN0_ALPHA_SWAP(x)		(((x)&1)<<13)
#define v_WIN0_MID_SWAP(x)		(((x)&1)<<14)
#define v_WIN0_UV_SWAP(x)		(((x)&1)<<15)
#define v_WIN0_PPAS_ZERO_EN(x)		(((x)&1)<<16)
#define v_WIN0_YRGB_DEFLICK(x)		(((x)&1)<<18)
#define v_WIN0_CBR_DEFLICK(x)		(((x)&1)<<19)
#define v_WIN0_YUV_CLIP(x)		(((x)&1)<<20)

#define m_WIN0_EN 			(1<<0)
#define m_WIN0_DATA_FMT 		(7<<1)
#define m_WIN0_FMT_10 			(1<<4)
#define m_WIN0_LB_MODE 			(7<<5)
#define m_WIN0_INTERLACE_READ		(1<<8)
#define m_WIN0_NO_OUTSTANDING 		(1<<9)
#define m_WIN0_CSC_MODE 		(3<<10)
#define m_WIN0_RB_SWAP 			(1<<12)
#define m_WIN0_ALPHA_SWAP 		(1<<13)
#define m_WIN0_MID_SWAP 		(1<<14)
#define m_WIN0_UV_SWAP 			(1<<15)
#define m_WIN0_PPAS_ZERO_EN     	(1<<16)
#define m_WIN0_YRGB_DEFLICK 		(1<<18)
#define m_WIN0_CBR_DEFLICK 		(1<<19)
#define m_WIN0_YUV_CLIP 		(1<<20)

#define WIN0_CTRL1 			(0x0034)
#define v_WIN0_YRGB_AXI_GATHER_EN(x)	(((x)&1)<<0)
#define v_WIN0_CBR_AXI_GATHER_EN(x)	(((x)&1)<<1)
#define v_WIN0_BIC_COE_SEL(x)           (((x)&3)<<2)
#define v_WIN0_VSD_YRGB_GT4(x)          (((x)&1)<<4)
#define v_WIN0_VSD_YRGB_GT2(x)          (((x)&1)<<5)
#define v_WIN0_VSD_CBR_GT4(x)           (((x)&1)<<6)
#define v_WIN0_VSD_CBR_GT2(x)           (((x)&1)<<7)
#define v_WIN0_YRGB_AXI_GATHER_NUM(x)	(((x)&0xf)<<8)
#define v_WIN0_CBR_AXI_GATHER_NUM(x)	(((x)&7)<<12)
#define v_WIN0_LINE_LOAD_MODE(x)	(((x)&1)<<15)
#define v_WIN0_YRGB_HOR_SCL_MODE(x)	(((x)&3)<<16)
#define v_WIN0_YRGB_VER_SCL_MODE(x)	(((x)&3)<<18)
#define v_WIN0_YRGB_HSD_MODE(x)		(((x)&3)<<20)
#define v_WIN0_YRGB_VSU_MODE(x)		(((x)&1)<<22)
#define v_WIN0_YRGB_VSD_MODE(x)		(((x)&1)<<23)
#define v_WIN0_CBR_HOR_SCL_MODE(x)	(((x)&3)<<24)
#define v_WIN0_CBR_VER_SCL_MODE(x)	(((x)&3)<<26)
#define v_WIN0_CBR_HSD_MODE(x)		(((x)&3)<<28)
#define v_WIN0_CBR_VSU_MODE(x)		(((x)&1)<<30)
#define v_WIN0_CBR_VSD_MODE(x)		(((x)&1)<<31)

#define m_WIN0_YRGB_AXI_GATHER_EN   	(1<<0)
#define m_WIN0_CBR_AXI_GATHER_EN        (1<<1)
#define m_WIN0_BIC_COE_SEL              (3<<2)
#define m_WIN0_VSD_YRGB_GT4             (1<<4)
#define m_WIN0_VSD_YRGB_GT2             (1<<5)
#define m_WIN0_VSD_CBR_GT4              (1<<6)
#define m_WIN0_VSD_CBR_GT2              (1<<7)
#define m_WIN0_YRGB_AXI_GATHER_NUM	(0xf<<8)
#define m_WIN0_CBR_AXI_GATHER_NUM	(7<<12)
#define m_WIN0_LINE_LOAD_MODE		(1<<15)
#define m_WIN0_YRGB_HOR_SCL_MODE	(3<<16)
#define m_WIN0_YRGB_VER_SCL_MODE	(3<<18)
#define m_WIN0_YRGB_HSD_MODE		(3<<20)
#define m_WIN0_YRGB_VSU_MODE		(1<<22)
#define m_WIN0_YRGB_VSD_MODE		(1<<23)
#define m_WIN0_CBR_HOR_SCL_MODE		(3<<24)
#define m_WIN0_CBR_VER_SCL_MODE		(3<<26)
#define m_WIN0_CBR_HSD_MODE		(3<<28)
#define m_WIN0_CBR_VSU_MODE		((u32)1<<30)
#define m_WIN0_CBR_VSD_MODE		((u32)1<<31)

#define WIN0_COLOR_KEY			(0x0038)
#define v_WIN0_COLOR_KEY(x)		(((x)&0x3fffffff)<<0)
#define v_WIN0_COLOR_KEY_EN(x)		(((x)&1)<<31)
#define m_WIN0_COLOR_KEY		(0x3fffffff<<0)
#define m_WIN0_COLOR_KEY_EN		((u32)1<<31)

#define WIN0_VIR 			(0x003c)
#define v_WIN0_VIR_STRIDE(x)		(((x)&0x3fff)<<0)
#define v_WIN0_VIR_STRIDE_UV(x)		(((x)&0x3fff)<<16)
#define m_WIN0_VIR_STRIDE		(0x3fff<<0)
#define m_WIN0_VIR_STRIDE_UV    	(0x3fff<<16)

#define WIN0_YRGB_MST 			(0x0040)
#define WIN0_CBR_MST 	        	(0x0044)
#define WIN0_ACT_INFO 			(0x0048)
#define v_WIN0_ACT_WIDTH(x)		(((x-1)&0x1fff)<<0)
#define v_WIN0_ACT_HEIGHT(x)		(((x-1)&0x1fff)<<16)
#define m_WIN0_ACT_WIDTH 		(0x1fff<<0)
#define m_WIN0_ACT_HEIGHT 		(0x1fff<<16)

#define WIN0_DSP_INFO 			(0x004c)
#define v_WIN0_DSP_WIDTH(x)		(((x-1)&0xfff)<<0)
#define v_WIN0_DSP_HEIGHT(x)		(((x-1)&0xfff)<<16)
#define m_WIN0_DSP_WIDTH 		(0xfff<<0)
#define m_WIN0_DSP_HEIGHT 		(0xfff<<16)

#define WIN0_DSP_ST 			(0x0050)
#define v_WIN0_DSP_XST(x)		(((x)&0x1fff)<<0)
#define v_WIN0_DSP_YST(x)		(((x)&0x1fff)<<16)
#define m_WIN0_DSP_XST 			(0x1fff<<0)
#define m_WIN0_DSP_YST 			(0x1fff<<16)

#define WIN0_SCL_FACTOR_YRGB 		(0x0054)
#define v_WIN0_HS_FACTOR_YRGB(x)	(((x)&0xffff)<<0)
#define v_WIN0_VS_FACTOR_YRGB(x)	(((x)&0xffff)<<16)
#define m_WIN0_HS_FACTOR_YRGB		(0xffff<<0)
#define m_WIN0_VS_FACTOR_YRGB		((u32)0xffff<<16)

#define WIN0_SCL_FACTOR_CBR 		(0x0058)
#define v_WIN0_HS_FACTOR_CBR(x)		(((x)&0xffff)<<0)
#define v_WIN0_VS_FACTOR_CBR(x)		(((x)&0xffff)<<16)
#define m_WIN0_HS_FACTOR_CBR		(0xffff<<0)
#define m_WIN0_VS_FACTOR_CBR		((u32)0xffff<<16)

#define WIN0_SCL_OFFSET 		(0x005c)
#define v_WIN0_HS_OFFSET_YRGB(x)	(((x)&0xff)<<0)
#define v_WIN0_HS_OFFSET_CBR(x)		(((x)&0xff)<<8)
#define v_WIN0_VS_OFFSET_YRGB(x)	(((x)&0xff)<<16)
#define v_WIN0_VS_OFFSET_CBR(x)		(((x)&0xff)<<24)

#define m_WIN0_HS_OFFSET_YRGB		(0xff<<0)
#define m_WIN0_HS_OFFSET_CBR		(0xff<<8)
#define m_WIN0_VS_OFFSET_YRGB		(0xff<<16)
#define m_WIN0_VS_OFFSET_CBR		((u32)0xff<<24)

#define WIN0_SRC_ALPHA_CTRL 		(0x0060)
#define v_WIN0_SRC_ALPHA_EN(x)		(((x)&1)<<0)
#define v_WIN0_SRC_COLOR_M0(x)		(((x)&1)<<1)
#define v_WIN0_SRC_ALPHA_M0(x)		(((x)&1)<<2)
#define v_WIN0_SRC_BLEND_M0(x)		(((x)&3)<<3)
#define v_WIN0_SRC_ALPHA_CAL_M0(x)	(((x)&1)<<5)
#define v_WIN0_SRC_FACTOR_M0(x)		(((x)&7)<<6)
#define v_WIN0_SRC_GLOBAL_ALPHA(x)	(((x)&0xff)<<16)
#define v_WIN0_FADING_VALUE(x)          (((x)&0xff)<<24)

#define m_WIN0_SRC_ALPHA_EN 		(1<<0)
#define m_WIN0_SRC_COLOR_M0 		(1<<1)
#define m_WIN0_SRC_ALPHA_M0 		(1<<2)
#define m_WIN0_SRC_BLEND_M0		(3<<3)
#define m_WIN0_SRC_ALPHA_CAL_M0		(1<<5)
#define m_WIN0_SRC_FACTOR_M0		(7<<6)
#define m_WIN0_SRC_GLOBAL_ALPHA		(0xff<<16)
#define m_WIN0_FADING_VALUE		(0xff<<24)

#define WIN0_DST_ALPHA_CTRL 		(0x0064)
#define v_WIN0_DST_FACTOR_M0(x)		(((x)&7)<<6)
#define m_WIN0_DST_FACTOR_M0		(7<<6)
 
#define WIN0_FADING_CTRL 		(0x0068)
#define v_WIN0_FADING_OFFSET_R(x)	(((x)&0xff)<<0)
#define v_WIN0_FADING_OFFSET_G(x)	(((x)&0xff)<<8)
#define v_WIN0_FADING_OFFSET_B(x)	(((x)&0xff)<<16)
#define v_WIN0_FADING_EN(x)		(((x)&1)<<24)

#define m_WIN0_FADING_OFFSET_R 		(0xff<<0)
#define m_WIN0_FADING_OFFSET_G 		(0xff<<8)
#define m_WIN0_FADING_OFFSET_B 		(0xff<<16)
#define m_WIN0_FADING_EN		(1<<24)

/*win1 register*/
#define WIN1_CTRL0 			(0x0070)
#define v_WIN1_EN(x)			(((x)&1)<<0)
#define v_WIN1_DATA_FMT(x)		(((x)&7)<<1)
#define v_WIN1_FMT_10(x)		(((x)&1)<<4)
#define v_WIN1_LB_MODE(x)		(((x)&7)<<5)
#define v_WIN1_INTERLACE_READ_MODE(x)	(((x)&1)<<8)
#define v_WIN1_NO_OUTSTANDING(x)	(((x)&1)<<9)
#define v_WIN1_CSC_MODE(x)		(((x)&3)<<10)
#define v_WIN1_RB_SWAP(x)		(((x)&1)<<12)
#define v_WIN1_ALPHA_SWAP(x)		(((x)&1)<<13)
#define v_WIN1_MID_SWAP(x)		(((x)&1)<<14)
#define v_WIN1_UV_SWAP(x)		(((x)&1)<<15)
#define v_WIN1_PPAS_ZERO_EN(x)		(((x)&1)<<16)
#define v_WIN1_YRGB_DEFLICK(x)		(((x)&1)<<18)
#define v_WIN1_CBR_DEFLICK(x)		(((x)&1)<<19)
#define v_WIN1_YUV_CLIP(x)		(((x)&1)<<20)

#define m_WIN1_EN			(1<<0)
#define m_WIN1_DATA_FMT			(7<<1)
#define m_WIN1_FMT_10			(1<<4)
#define m_WIN1_LB_MODE			(7<<5)
#define m_WIN1_INTERLACE_READ_MODE 	(1<<8)
#define m_WIN1_NO_OUTSTANDING 		(1<<9)
#define m_WIN1_CSC_MODE 		(3<<10)
#define m_WIN1_RB_SWAP 			(1<<12)
#define m_WIN1_ALPHA_SWAP 		(1<<13)
#define m_WIN1_MID_SWAP 		(1<<14)
#define m_WIN1_UV_SWAP 			(1<<15)
#define m_WIN1_PPAS_ZERO_EN             (1<<16)
#define m_WIN1_YRGB_DEFLICK 		(1<<18)
#define m_WIN1_CBR_DEFLICK 		(1<<19)
#define m_WIN1_YUV_CLIP 		(1<<20)

#define WIN1_CTRL1 			(0x0074)
#define v_WIN1_YRGB_AXI_GATHER_EN(x)	(((x)&1)<<0)
#define v_WIN1_CBR_AXI_GATHER_EN(x)	(((x)&1)<<1)
#define v_WIN1_BIC_COE_SEL(x)           (((x)&3)<<2)
#define v_WIN1_VSD_YRGB_GT4(x)          (((x)&1)<<4)
#define v_WIN1_VSD_YRGB_GT2(x)          (((x)&1)<<5)
#define v_WIN1_VSD_CBR_GT4(x)           (((x)&1)<<6)
#define v_WIN1_VSD_CBR_GT2(x)           (((x)&1)<<7)
#define v_WIN1_YRGB_AXI_GATHER_NUM(x)	(((x)&0xf)<<8)
#define v_WIN1_CBR_AXI_GATHER_NUM(x)	(((x)&7)<<12)
#define v_WIN1_LINE_LOAD_MODE(x)	(((x)&1)<<15)
#define v_WIN1_YRGB_HOR_SCL_MODE(x)	(((x)&3)<<16)
#define v_WIN1_YRGB_VER_SCL_MODE(x)	(((x)&3)<<18)
#define v_WIN1_YRGB_HSD_MODE(x)		(((x)&3)<<20)
#define v_WIN1_YRGB_VSU_MODE(x)		(((x)&1)<<22)
#define v_WIN1_YRGB_VSD_MODE(x)		(((x)&1)<<23)
#define v_WIN1_CBR_HOR_SCL_MODE(x)	(((x)&3)<<24)
#define v_WIN1_CBR_VER_SCL_MODE(x)	(((x)&3)<<26)
#define v_WIN1_CBR_HSD_MODE(x)		(((x)&3)<<28)
#define v_WIN1_CBR_VSU_MODE(x)		(((x)&1)<<30)
#define v_WIN1_CBR_VSD_MODE(x)		(((x)&1)<<31)

#define m_WIN1_YRGB_AXI_GATHER_EN	(1<<0)
#define m_WIN1_CBR_AXI_GATHER_EN	(1<<1)
#define m_WIN1_BIC_COE_SEL              (3<<2)
#define m_WIN1_VSD_YRGB_GT4             (1<<4)
#define m_WIN1_VSD_YRGB_GT2             (1<<5)
#define m_WIN1_VSD_CBR_GT4              (1<<6)
#define m_WIN1_VSD_CBR_GT2              (1<<7)
#define m_WIN1_YRGB_AXI_GATHER_NUM	(0xf<<8)
#define m_WIN1_CBR_AXI_GATHER_NUM	(7<<12)
#define m_WIN1_LINE_LOAD_MODE		(1<<15)
#define m_WIN1_YRGB_HOR_SCL_MODE	(3<<16)
#define m_WIN1_YRGB_VER_SCL_MODE	(3<<18)
#define m_WIN1_YRGB_HSD_MODE		(3<<20)
#define m_WIN1_YRGB_VSU_MODE		(1<<22)
#define m_WIN1_YRGB_VSD_MODE		(1<<23)
#define m_WIN1_CBR_HOR_SCL_MODE		(3<<24)
#define m_WIN1_CBR_VER_SCL_MODE		(3<<26)
#define m_WIN1_CBR_HSD_MODE		(3<<28)
#define m_WIN1_CBR_VSU_MODE		(1<<30)
#define m_WIN1_CBR_VSD_MODE		((u32)1<<31)

#define WIN1_COLOR_KEY 			(0x0078)
#define v_WIN1_COLOR_KEY(x)		(((x)&0x3fffffff)<<0)
#define v_WIN1_COLOR_KEY_EN(x)		(((x)&1)<<31)
#define m_WIN1_COLOR_KEY		(0x3fffffff<<0)
#define m_WIN1_COLOR_KEY_EN		((u32)1<<31)

#define WIN1_VIR 			(0x007c)
#define v_WIN1_VIR_STRIDE(x)		(((x)&0x3fff)<<0)
#define v_WIN1_VIR_STRIDE_UV(x)		(((x)&0x3fff)<<16)
#define m_WIN1_VIR_STRIDE 		(0x3fff<<0)
#define m_WIN1_VIR_STRIDE_UV 		(0x3fff<<16)

#define WIN1_YRGB_MST 			(0x0080)
#define WIN1_CBR_MST 			(0x0084)
#define WIN1_ACT_INFO 			(0x0088)
#define v_WIN1_ACT_WIDTH(x)		(((x-1)&0x1fff)<<0)
#define v_WIN1_ACT_HEIGHT(x)		(((x-1)&0x1fff)<<16)
#define m_WIN1_ACT_WIDTH		(0x1fff<<0)
#define m_WIN1_ACT_HEIGHT		(0x1fff<<16)

#define WIN1_DSP_INFO 			(0x008c)
#define v_WIN1_DSP_WIDTH(x)		(((x-1)&0xfff)<<0)
#define v_WIN1_DSP_HEIGHT(x)		(((x-1)&0xfff)<<16)
#define m_WIN1_DSP_WIDTH 		(0xfff<<0)
#define m_WIN1_DSP_HEIGHT 		(0xfff<<16)

#define WIN1_DSP_ST 			(0x0090)
#define v_WIN1_DSP_XST(x)		(((x)&0x1fff)<<0)
#define v_WIN1_DSP_YST(x)		(((x)&0x1fff)<<16)
#define m_WIN1_DSP_XST 			(0x1fff<<0)
#define m_WIN1_DSP_YST 			(0x1fff<<16)

#define WIN1_SCL_FACTOR_YRGB 		(0x0094)
#define v_WIN1_HS_FACTOR_YRGB(x)	(((x)&0xffff)<<0)
#define v_WIN1_VS_FACTOR_YRGB(x)	(((x)&0xffff)<<16)
#define m_WIN1_HS_FACTOR_YRGB 		(0xffff<<0)
#define m_WIN1_VS_FACTOR_YRGB 		((u32)0xffff<<16)

#define WIN1_SCL_FACTOR_CBR 		(0x0098)
#define v_WIN1_HS_FACTOR_CBR(x)		(((x)&0xffff)<<0)
#define v_WIN1_VS_FACTOR_CBR(x)		(((x)&0xffff)<<16)
#define m_WIN1_HS_FACTOR_CBR		(0xffff<<0)
#define m_WIN1_VS_FACTOR_CBR		((u32)0xffff<<16)

#define WIN1_SCL_OFFSET 		(0x009c)
#define v_WIN1_HS_OFFSET_YRGB(x)	(((x)&0xff)<<0)
#define v_WIN1_HS_OFFSET_CBR(x)		(((x)&0xff)<<8)
#define v_WIN1_VS_OFFSET_YRGB(x)	(((x)&0xff)<<16)
#define v_WIN1_VS_OFFSET_CBR(x)		(((x)&0xff)<<24)

#define m_WIN1_HS_OFFSET_YRGB		(0xff<<0)
#define m_WIN1_HS_OFFSET_CBR		(0xff<<8)
#define m_WIN1_VS_OFFSET_YRGB		(0xff<<16)
#define m_WIN1_VS_OFFSET_CBR		((u32)0xff<<24)

#define WIN1_SRC_ALPHA_CTRL 		(0x00a0)
#define v_WIN1_SRC_ALPHA_EN(x)		(((x)&1)<<0)
#define v_WIN1_SRC_COLOR_M0(x)		(((x)&1)<<1)
#define v_WIN1_SRC_ALPHA_M0(x)		(((x)&1)<<2)
#define v_WIN1_SRC_BLEND_M0(x)		(((x)&3)<<3)
#define v_WIN1_SRC_ALPHA_CAL_M0(x)	(((x)&1)<<5)
#define v_WIN1_SRC_FACTOR_M0(x)		(((x)&7)<<6)
#define v_WIN1_SRC_GLOBAL_ALPHA(x)	(((x)&0xff)<<16)
#define v_WIN1_FADING_VALUE(x)          (((x)&0xff)<<24)

#define m_WIN1_SRC_ALPHA_EN 		(1<<0)
#define m_WIN1_SRC_COLOR_M0 		(1<<1)
#define m_WIN1_SRC_ALPHA_M0 		(1<<2)
#define m_WIN1_SRC_BLEND_M0		(3<<3)
#define m_WIN1_SRC_ALPHA_CAL_M0		(1<<5)
#define m_WIN1_SRC_FACTOR_M0		(7<<6)
#define m_WIN1_SRC_GLOBAL_ALPHA		(0xff<<16)
#define m_WIN1_FADING_VALUE		(0xff<<24)

#define WIN1_DST_ALPHA_CTRL 		(0x00a4)
#define v_WIN1_DST_FACTOR_M0(x)		(((x)&7)<<6)
#define m_WIN1_DST_FACTOR_M0		(7<<6)

#define WIN1_FADING_CTRL 		(0x00a8)
#define v_WIN1_FADING_OFFSET_R(x)	(((x)&0xff)<<0)
#define v_WIN1_FADING_OFFSET_G(x)	(((x)&0xff)<<8)
#define v_WIN1_FADING_OFFSET_B(x)	(((x)&0xff)<<16)
#define v_WIN1_FADING_EN(x)		(((x)&1)<<24)

#define m_WIN1_FADING_OFFSET_R 		(0xff<<0)
#define m_WIN1_FADING_OFFSET_G 		(0xff<<8)
#define m_WIN1_FADING_OFFSET_B 		(0xff<<16)
#define m_WIN1_FADING_EN		(1<<24)

/*win2 register*/
#define WIN2_CTRL0 			(0x00b0)
#define v_WIN2_EN(x)			(((x)&1)<<0)
#define v_WIN2_DATA_FMT(x)		(((x)&7)<<1)
#define v_WIN2_MST0_EN(x)		(((x)&1)<<4)
#define v_WIN2_MST1_EN(x)		(((x)&1)<<5)
#define v_WIN2_MST2_EN(x)		(((x)&1)<<6)
#define v_WIN2_MST3_EN(x)		(((x)&1)<<7)
#define v_WIN2_INTERLACE_READ(x)	(((x)&1)<<8)
#define v_WIN2_NO_OUTSTANDING(x)	(((x)&1)<<9)
#define v_WIN2_CSC_MODE(x)		(((x)&1)<<10)
#define v_WIN2_RB_SWAP(x)		(((x)&1)<<12)
#define v_WIN2_ALPHA_SWAP(x)		(((x)&1)<<13)
#define v_WIN2_ENDIAN_MODE(x)		(((x)&1)<<14)
#define v_WIN2_LUT_EN(x)		(((x)&1)<<18)

#define m_WIN2_EN 			(1<<0)
#define m_WIN2_DATA_FMT 		(7<<1)
#define m_WIN2_MST0_EN 			(1<<4)
#define m_WIN2_MST1_EN 			(1<<5)
#define m_WIN2_MST2_EN 			(1<<6)
#define m_WIN2_MST3_EN 			(1<<7)
#define m_WIN2_INTERLACE_READ 		(1<<8)
#define m_WIN2_NO_OUTSTANDING 		(1<<9)
#define m_WIN2_CSC_MODE 		(1<<10)
#define m_WIN2_RB_SWAP 			(1<<12)
#define m_WIN2_ALPHA_SWAP 		(1<<13)
#define m_WIN2_ENDIAN_MODE 		(1<<14)
#define m_WIN2_LUT_EN 			(1<<18)

#define WIN2_CTRL1 			(0x00b4)
#define v_WIN2_AXI_GATHER_EN(x)		(((x)&1)<<0)
#define v_WIN2_AXI_GATHER_NUM(x)	(((x)&0xf)<<4)
#define m_WIN2_AXI_GATHER_EN		(1<<0)
#define m_WIN2_AXI_GATHER_NUM		(0xf<<4)

#define WIN2_VIR0_1 			(0x00b8)
#define v_WIN2_VIR_STRIDE0(x)		(((x)&0x1fff)<<0)
#define v_WIN2_VIR_STRIDE1(x)		(((x)&0x1fff)<<16)
#define m_WIN2_VIR_STRIDE0		(0x1fff<<0)
#define m_WIN2_VIR_STRIDE1		(0x1fff<<16)

#define WIN2_VIR2_3 			(0x00bc)
#define v_WIN2_VIR_STRIDE2(x)		(((x)&0x1fff)<<0)
#define v_WIN2_VIR_STRIDE3(x)		(((x)&0x1fff)<<16)
#define m_WIN2_VIR_STRIDE2		(0x1fff<<0)
#define m_WIN2_VIR_STRIDE3		(0x1fff<<16)

#define WIN2_MST0 			(0x00c0)
#define WIN2_DSP_INFO0 			(0x00c4)
#define v_WIN2_DSP_WIDTH0(x)		(((x-1)&0xfff)<<0)
#define v_WIN2_DSP_HEIGHT0(x)		(((x-1)&0xfff)<<16)
#define m_WIN2_DSP_WIDTH0		(0xfff<<0)
#define m_WIN2_DSP_HEIGHT0		(0xfff<<16)

#define WIN2_DSP_ST0 			(0x00c8)
#define v_WIN2_DSP_XST0(x)		(((x)&0x1fff)<<0)
#define v_WIN2_DSP_YST0(x)		(((x)&0x1fff)<<16)
#define m_WIN2_DSP_XST0			(0x1fff<<0)
#define m_WIN2_DSP_YST0			(0x1fff<<16)

#define WIN2_COLOR_KEY 			(0x00cc)
#define v_WIN2_COLOR_KEY(x)		(((x)&0xffffff)<<0)
#define v_WIN2_KEY_EN(x)		(((x)&1)<<24)
#define m_WIN2_COLOR_KEY		(0xffffff<<0)
#define m_WIN2_KEY_EN			((u32)1<<24)


#define WIN2_MST1               	(0x00d0) 
#define WIN2_DSP_INFO1 			(0x00d4)
#define v_WIN2_DSP_WIDTH1(x)		(((x-1)&0xfff)<<0)
#define v_WIN2_DSP_HEIGHT1(x)		(((x-1)&0xfff)<<16)

#define m_WIN2_DSP_WIDTH1		(0xfff<<0)
#define m_WIN2_DSP_HEIGHT1		(0xfff<<16)

#define WIN2_DSP_ST1 	        	(0x00d8)
#define v_WIN2_DSP_XST1(x)		(((x)&0x1fff)<<0)
#define v_WIN2_DSP_YST1(x)		(((x)&0x1fff)<<16)

#define m_WIN2_DSP_XST1			(0x1fff<<0)
#define m_WIN2_DSP_YST1			(0x1fff<<16)

#define WIN2_SRC_ALPHA_CTRL 		(0x00dc)
#define v_WIN2_SRC_ALPHA_EN(x)		(((x)&1)<<0)
#define v_WIN2_SRC_COLOR_M0(x)		(((x)&1)<<1)
#define v_WIN2_SRC_ALPHA_M0(x)		(((x)&1)<<2)
#define v_WIN2_SRC_BLEND_M0(x)		(((x)&3)<<3)
#define v_WIN2_SRC_ALPHA_CAL_M0(x)	(((x)&1)<<5)
#define v_WIN2_SRC_FACTOR_M0(x)		(((x)&7)<<6)
#define v_WIN2_SRC_GLOBAL_ALPHA(x)	(((x)&0xff)<<16)
#define v_WIN2_FADING_VALUE(x)          (((x)&0xff)<<24)


#define m_WIN2_SRC_ALPHA_EN 		(1<<0)
#define m_WIN2_SRC_COLOR_M0 		(1<<1)
#define m_WIN2_SRC_ALPHA_M0 		(1<<2)
#define m_WIN2_SRC_BLEND_M0		(3<<3)
#define m_WIN2_SRC_ALPHA_CAL_M0		(1<<5)
#define m_WIN2_SRC_FACTOR_M0		(7<<6)
#define m_WIN2_SRC_GLOBAL_ALPHA		(0xff<<16)
#define m_WIN2_FADING_VALUE		(0xff<<24)

#define WIN2_MST2 			(0x00e0)
#define WIN2_DSP_INFO2 			(0x00e4)
#define v_WIN2_DSP_WIDTH2(x)		(((x-1)&0xfff)<<0)
#define v_WIN2_DSP_HEIGHT2(x)		(((x-1)&0xfff)<<16)

#define m_WIN2_DSP_WIDTH2 		(0xfff<<0)
#define m_WIN2_DSP_HEIGHT2 		(0xfff<<16)


#define WIN2_DSP_ST2 			(0x00e8)
#define v_WIN2_DSP_XST2(x)		(((x)&0x1fff)<<0)
#define v_WIN2_DSP_YST2(x)		(((x)&0x1fff)<<16)
#define m_WIN2_DSP_XST2 		(0x1fff<<0)
#define m_WIN2_DSP_YST2 		(0x1fff<<16)

#define WIN2_DST_ALPHA_CTRL 		(0x00ec)
#define v_WIN2_DST_FACTOR_M0(x)		(((x)&7)<<6)
#define m_WIN2_DST_FACTOR_M0		(7<<6)

#define WIN2_MST3 			(0x00f0)
#define WIN2_DSP_INFO3 			(0x00f4)
#define v_WIN2_DSP_WIDTH3(x)		(((x-1)&0xfff)<<0)
#define v_WIN2_DSP_HEIGHT3(x)		(((x-1)&0xfff)<<16)
#define m_WIN2_DSP_WIDTH3		(0xfff<<0)
#define m_WIN2_DSP_HEIGHT3		(0xfff<<16)

#define WIN2_DSP_ST3 			(0x00f8)
#define v_WIN2_DSP_XST3(x)		(((x)&0x1fff)<<0)
#define v_WIN2_DSP_YST3(x)		(((x)&0x1fff)<<16)
#define m_WIN2_DSP_XST3 		(0x1fff<<0)
#define m_WIN2_DSP_YST3 		(0x1fff<<16)

#define WIN2_FADING_CTRL 		(0x00fc)
#define v_WIN2_FADING_OFFSET_R(x)	(((x)&0xff)<<0)
#define v_WIN2_FADING_OFFSET_G(x)	(((x)&0xff)<<8)
#define v_WIN2_FADING_OFFSET_B(x)	(((x)&0xff)<<16)
#define v_WIN2_FADING_EN(x)		(((x)&1)<<24)

#define m_WIN2_FADING_OFFSET_R 		(0xff<<0)
#define m_WIN2_FADING_OFFSET_G 		(0xff<<8)
#define m_WIN2_FADING_OFFSET_B 		(0xff<<16)
#define m_WIN2_FADING_EN		(1<<24)

/*win3 register*/
#define WIN3_CTRL0 			(0x0100)
#define v_WIN3_EN(x)			(((x)&1)<<0)
#define v_WIN3_DATA_FMT(x)		(((x)&7)<<1)
#define v_WIN3_MST0_EN(x)		(((x)&1)<<4)
#define v_WIN3_MST1_EN(x)		(((x)&1)<<5)
#define v_WIN3_MST2_EN(x)		(((x)&1)<<6)
#define v_WIN3_MST3_EN(x)		(((x)&1)<<7)
#define v_WIN3_INTERLACE_READ(x)	(((x)&1)<<8)
#define v_WIN3_NO_OUTSTANDING(x)	(((x)&1)<<9)
#define v_WIN3_CSC_MODE(x)		(((x)&1)<<10)
#define v_WIN3_RB_SWAP(x)		(((x)&1)<<12)
#define v_WIN3_ALPHA_SWAP(x)		(((x)&1)<<13)
#define v_WIN3_ENDIAN_MODE(x)		(((x)&1)<<14)
#define v_WIN3_LUT_EN(x)		(((x)&1)<<18)

#define m_WIN3_EN 			(1<<0)
#define m_WIN3_DATA_FMT 		(7<<1)
#define m_WIN3_MST0_EN 			(1<<4)
#define m_WIN3_MST1_EN 			(1<<5)
#define m_WIN3_MST2_EN 			(1<<6)
#define m_WIN3_MST3_EN 			(1<<7)
#define m_WIN3_INTERLACE_READ 		(1<<8)
#define m_WIN3_NO_OUTSTANDING 		(1<<9)
#define m_WIN3_CSC_MODE 		(1<<10)
#define m_WIN3_RB_SWAP 			(1<<12)
#define m_WIN3_ALPHA_SWAP 		(1<<13)
#define m_WIN3_ENDIAN_MODE 		(1<<14)
#define m_WIN3_LUT_EN 			(1<<18)


#define WIN3_CTRL1 			(0x0104)
#define v_WIN3_AXI_GATHER_EN(x)		(((x)&1)<<0)
#define v_WIN3_AXI_GATHER_NUM(x)	(((x)&0xf)<<4)
#define m_WIN3_AXI_GATHER_EN 		(1<<0)
#define m_WIN3_AXI_GATHER_NUM 		(0xf<<4)

#define WIN3_VIR0_1 			(0x0108)
#define v_WIN3_VIR_STRIDE0(x)		(((x)&0x1fff)<<0)
#define v_WIN3_VIR_STRIDE1(x)		(((x)&0x1fff)<<16)
#define m_WIN3_VIR_STRIDE0 		(0x1fff<<0)
#define m_WIN3_VIR_STRIDE1 		(0x1fff<<16)

#define WIN3_VIR2_3 			(0x010c)
#define v_WIN3_VIR_STRIDE2(x)		(((x)&0x1fff)<<0)
#define v_WIN3_VIR_STRIDE3(x)		(((x)&0x1fff)<<16)
#define m_WIN3_VIR_STRIDE2		(0x1fff<<0)
#define m_WIN3_VIR_STRIDE3		(0x1fff<<16)

#define WIN3_MST0 	        	(0x0110)
#define WIN3_DSP_INFO0 			(0x0114)
#define v_WIN3_DSP_WIDTH0(x)		(((x-1)&0xfff)<<0)
#define v_WIN3_DSP_HEIGHT0(x)		(((x-1)&0xfff)<<16)
#define m_WIN3_DSP_WIDTH0 		(0xfff<<0)
#define m_WIN3_DSP_HEIGHT0 		(0xfff<<16)

#define WIN3_DSP_ST0 			(0x0118)
#define v_WIN3_DSP_XST0(x)		(((x)&0x1fff)<<0)
#define v_WIN3_DSP_YST0(x)		(((x)&0x1fff)<<16)
#define m_WIN3_DSP_XST0			(0x1fff<<0)
#define m_WIN3_DSP_YST0			(0x1fff<<16)

#define WIN3_COLOR_KEY 			(0x011c)
#define v_WIN3_COLOR_KEY(x)		(((x)&0xffffff)<<0)
#define v_WIN3_KEY_EN(x)		(((x)&1)<<24)
#define m_WIN3_COLOR_KEY		(0xffffff<<0)
#define m_WIN3_KEY_EN			((u32)1<<24)

#define WIN3_MST1 			(0x0120)
#define WIN3_DSP_INFO1 			(0x0124)
#define v_WIN3_DSP_WIDTH1(x)		(((x-1)&0xfff)<<0)
#define v_WIN3_DSP_HEIGHT1(x)		(((x-1)&0xfff)<<16)
#define m_WIN3_DSP_WIDTH1 		(0xfff<<0)
#define m_WIN3_DSP_HEIGHT1 		(0xfff<<16)

#define WIN3_DSP_ST1 			(0x0128)
#define v_WIN3_DSP_XST1(x)		(((x)&0x1fff)<<0)
#define v_WIN3_DSP_YST1(x)		(((x)&0x1fff)<<16)
#define m_WIN3_DSP_XST1			(0x1fff<<0)
#define m_WIN3_DSP_YST1			(0x1fff<<16)

#define WIN3_SRC_ALPHA_CTRL 		(0x012c)
#define v_WIN3_SRC_ALPHA_EN(x)		(((x)&1)<<0)
#define v_WIN3_SRC_COLOR_M0(x)		(((x)&1)<<1)
#define v_WIN3_SRC_ALPHA_M0(x)		(((x)&1)<<2)
#define v_WIN3_SRC_BLEND_M0(x)		(((x)&3)<<3)
#define v_WIN3_SRC_ALPHA_CAL_M0(x)	(((x)&1)<<5)
#define v_WIN3_SRC_FACTOR_M0(x)		(((x)&7)<<6)
#define v_WIN3_SRC_GLOBAL_ALPHA(x)	(((x)&0xff)<<16)
#define v_WIN3_FADING_VALUE(x)          (((x)&0xff)<<24)

#define m_WIN3_SRC_ALPHA_EN 		(1<<0)
#define m_WIN3_SRC_COLOR_M0 		(1<<1)
#define m_WIN3_SRC_ALPHA_M0 		(1<<2)
#define m_WIN3_SRC_BLEND_M0		(3<<3)
#define m_WIN3_SRC_ALPHA_CAL_M0		(1<<5)
#define m_WIN3_SRC_FACTOR_M0		(7<<6)
#define m_WIN3_SRC_GLOBAL_ALPHA		(0xff<<16)
#define m_WIN3_FADING_VALUE		(0xff<<24)

#define WIN3_MST2 			(0x0130)
#define WIN3_DSP_INFO2 			(0x0134)
#define v_WIN3_DSP_WIDTH2(x)		(((x-1)&0xfff)<<0)
#define v_WIN3_DSP_HEIGHT2(x)		(((x-1)&0xfff)<<16)
#define m_WIN3_DSP_WIDTH2		(0xfff<<0)
#define m_WIN3_DSP_HEIGHT2		(0xfff<<16)

#define WIN3_DSP_ST2 			(0x0138)
#define v_WIN3_DSP_XST2(x)		(((x)&0x1fff)<<0)
#define v_WIN3_DSP_YST2(x)		(((x)&0x1fff)<<16)
#define m_WIN3_DSP_XST2			(0x1fff<<0)
#define m_WIN3_DSP_YST2			(0x1fff<<16)

#define WIN3_DST_ALPHA_CTRL 		(0x013c)
#define v_WIN3_DST_FACTOR_M0(x)		(((x)&7)<<6)
#define m_WIN3_DST_FACTOR_M0		(7<<6)


#define WIN3_MST3 			(0x0140)
#define WIN3_DSP_INFO3 			(0x0144)
#define v_WIN3_DSP_WIDTH3(x)		(((x-1)&0xfff)<<0)
#define v_WIN3_DSP_HEIGHT3(x)		(((x-1)&0xfff)<<16)
#define m_WIN3_DSP_WIDTH3		(0xfff<<0)
#define m_WIN3_DSP_HEIGHT3		(0xfff<<16)

#define WIN3_DSP_ST3 			(0x0148)
#define v_WIN3_DSP_XST3(x)		(((x)&0x1fff)<<0)
#define v_WIN3_DSP_YST3(x)		(((x)&0x1fff)<<16)
#define m_WIN3_DSP_XST3			(0x1fff<<0)
#define m_WIN3_DSP_YST3			(0x1fff<<16)

#define WIN3_FADING_CTRL 		(0x014c)
#define v_WIN3_FADING_OFFSET_R(x)	(((x)&0xff)<<0)
#define v_WIN3_FADING_OFFSET_G(x)	(((x)&0xff)<<8)
#define v_WIN3_FADING_OFFSET_B(x)	(((x)&0xff)<<16)
#define v_WIN3_FADING_EN(x)		(((x)&1)<<24)

#define m_WIN3_FADING_OFFSET_R 		(0xff<<0)
#define m_WIN3_FADING_OFFSET_G 		(0xff<<8)
#define m_WIN3_FADING_OFFSET_B 		(0xff<<16)
#define m_WIN3_FADING_EN		(1<<24)


/*hwc register*/
#define HWC_CTRL0 			(0x0150)
#define v_HWC_EN(x)			(((x)&1)<<0)
#define v_HWC_DATA_FMT(x)		(((x)&7)<<1)
#define v_HWC_MODE(x)			(((x)&1)<<4)
#define v_HWC_SIZE(x)			(((x)&3)<<5)
#define v_HWC_INTERLACE_READ(x)		(((x)&1)<<8)
#define v_HWC_NO_OUTSTANDING(x)		(((x)&1)<<9)
#define v_HWC_CSC_MODE(x)		(((x)&1)<<10)
#define v_HWC_RB_SWAP(x)		(((x)&1)<<12)
#define v_HWC_ALPHA_SWAP(x)		(((x)&1)<<13)
#define v_HWC_ENDIAN_MODE(x)		(((x)&1)<<14)
#define v_HWC_LUT_EN(x)			(((x)&1)<<18)

#define m_HWC_EN			(1<<0)
#define m_HWC_DATA_FMT			(7<<1)
#define m_HWC_MODE			(1<<4)
#define m_HWC_SIZE			(3<<5)
#define m_HWC_INTERLACE_READ 		(1<<8)
#define m_HWC_NO_OUTSTANDING		(1<<9)
#define m_HWC_CSC_MODE			(1<<10)
#define m_HWC_RB_SWAP			(1<<12)
#define m_HWC_ALPHA_SWAP		(1<<13)
#define m_HWC_ENDIAN_MODE		(1<<14)
#define m_HWC_LUT_EN			(1<<18)


#define HWC_CTRL1 			(0x0154)
#define v_HWC_AXI_GATHER_EN(x)		(((x)&1)<<0)
#define v_HWC_AXI_GATHER_NUM(x)		(((x)&7)<<4)
#define m_HWC_AXI_GATHER_EN		(1<<0)
#define m_HWC_AXI_GATHER_NUM		(7<<4)

#define HWC_MST 			(0x0158)
#define HWC_DSP_ST 			(0x015c)
#define v_HWC_DSP_XST(x)		(((x)&0x1fff)<<0)
#define v_HWC_DSP_YST(x)		(((x)&0x1fff)<<16)
#define m_HWC_DSP_XST			(0x1fff<<0)
#define m_HWC_DSP_YST			(0x1fff<<16)

#define HWC_SRC_ALPHA_CTRL		(0x0160)
#define v_HWC_SRC_ALPHA_EN(x)		(((x)&1)<<0)
#define v_HWC_SRC_COLOR_M0(x)		(((x)&1)<<1)
#define v_HWC_SRC_ALPHA_M0(x)		(((x)&1)<<2)
#define v_HWC_SRC_BLEND_M0(x)		(((x)&3)<<3)
#define v_HWC_SRC_ALPHA_CAL_M0(x)	(((x)&1)<<5)
#define v_HWC_SRC_FACTOR_M0(x)		(((x)&7)<<6)
#define v_HWC_SRC_GLOBAL_ALPHA(x)	(((x)&0xff)<<16)
#define v_HWC_FADING_VALUE(x)           (((x)&0xff)<<24)

#define m_HWC_SRC_ALPHA_EN 		(1<<0)
#define m_HWC_SRC_COLOR_M0 		(1<<1)
#define m_HWC_SRC_ALPHA_M0 		(1<<2)
#define m_HWC_SRC_BLEND_M0		(3<<3)
#define m_HWC_SRC_ALPHA_CAL_M0		(1<<5)
#define m_HWC_SRC_FACTOR_M0		(7<<6)
#define m_HWC_SRC_GLOBAL_ALPHA		(0xff<<16)
#define m_HWC_FADING_VALUE		(0xff<<24)

#define HWC_DST_ALPHA_CTRL 		(0x0164)
#define v_HWC_DST_FACTOR_M0(x)		(((x)&7)<<6)
#define m_HWC_DST_FACTOR_M0		(7<<6)


#define HWC_FADING_CTRL 		(0x0168)
#define v_HWC_FADING_OFFSET_R(x)        (((x)&0xff)<<0)
#define v_HWC_FADING_OFFSET_G(x)        (((x)&0xff)<<8)
#define v_HWC_FADING_OFFSET_B(x)        (((x)&0xff)<<16)
#define v_HWC_FADING_EN(x)	        (((x)&1)<<24)

#define m_HWC_FADING_OFFSET_R 	        (0xff<<0)
#define m_HWC_FADING_OFFSET_G 	        (0xff<<8)
#define m_HWC_FADING_OFFSET_B 	        (0xff<<16)
#define m_HWC_FADING_EN                 (1<<24)

/*post process register*/
#define POST_DSP_HACT_INFO 		(0x0170)
#define v_DSP_HACT_END_POST(x)		(((x)&0x1fff)<<0)
#define v_DSP_HACT_ST_POST(x)		(((x)&0x1fff)<<16)
#define m_DSP_HACT_END_POST		(0x1fff<<0)
#define m_DSP_HACT_ST_POST		(0x1fff<<16)

#define POST_DSP_VACT_INFO 		(0x0174)
#define v_DSP_VACT_END_POST(x)		(((x)&0x1fff)<<0)
#define v_DSP_VACT_ST_POST(x)		(((x)&0x1fff)<<16)
#define m_DSP_VACT_END_POST		(0x1fff<<0)
#define m_DSP_VACT_ST_POST		(0x1fff<<16)

#define POST_SCL_FACTOR_YRGB 		(0x0178)
#define v_POST_HS_FACTOR_YRGB(x)	(((x)&0xffff)<<0)
#define v_POST_VS_FACTOR_YRGB(x)	(((x)&0xffff)<<16)
#define m_POST_HS_FACTOR_YRGB		(0xffff<<0)
#define m_POST_VS_FACTOR_YRGB		(0xffff<<16)

#define POST_SCL_CTRL 			(0x0180)
#define v_POST_HOR_SD_EN(x)		(((x)&1)<<0)
#define v_POST_VER_SD_EN(x)		(((x)&1)<<1)

#define m_POST_HOR_SD_EN		(0x1<<0)
#define m_POST_VER_SD_EN		(0x1<<1)

#define POST_DSP_VACT_INFO_F1 		(0x0184)
#define v_DSP_VACT_END_POST_F1(x)       (((x)&0x1fff)<<0)
#define v_DSP_VACT_ST_POST_F1(x)        (((x)&0x1fff)<<16)

#define m_DSP_VACT_END_POST_F1          (0x1fff<<0)
#define m_DSP_VACT_ST_POST_F1           (0x1fff<<16)

#define DSP_HTOTAL_HS_END 		(0x0188)
#define v_DSP_HS_PW(x)			(((x)&0x1fff)<<0)
#define v_DSP_HTOTAL(x)			(((x)&0x1fff)<<16)
#define m_DSP_HS_PW			(0x1fff<<0)
#define m_DSP_HTOTAL			(0x1fff<<16)

#define DSP_HACT_ST_END 		(0x018c)
#define v_DSP_HACT_END(x)		(((x)&0x1fff)<<0)
#define v_DSP_HACT_ST(x)		(((x)&0x1fff)<<16)
#define m_DSP_HACT_END			(0x1fff<<0)
#define m_DSP_HACT_ST			(0x1fff<<16)

#define DSP_VTOTAL_VS_END 		(0x0190)
#define v_DSP_VS_PW(x)			(((x)&0x1fff)<<0)
#define v_DSP_VTOTAL(x)			(((x)&0x1fff)<<16)
#define m_DSP_VS_PW			(0x1fff<<0)
#define m_DSP_VTOTAL			(0x1fff<<16)

#define DSP_VACT_ST_END 		(0x0194)
#define v_DSP_VACT_END(x)		(((x)&0x1fff)<<0)
#define v_DSP_VACT_ST(x)		(((x)&0x1fff)<<16)
#define m_DSP_VACT_END			(0x1fff<<0)
#define m_DSP_VACT_ST			(0x1fff<<16)

#define DSP_VS_ST_END_F1 		(0x0198)
#define v_DSP_VS_END_F1(x)		(((x)&0x1fff)<<0)
#define v_DSP_VS_ST_F1(x)		(((x)&0x1fff)<<16)
#define m_DSP_VS_END_F1			(0x1fff<<0)
#define m_DSP_VS_ST_F1			(0x1fff<<16)

#define DSP_VACT_ST_END_F1 		(0x019c)
#define v_DSP_VACT_END_F1(x)		(((x)&0x1fff)<<0)
#define v_DSP_VAC_ST_F1(x)		(((x)&0x1fff)<<16)
#define m_DSP_VACT_END_F1		(0x1fff<<0)
#define m_DSP_VAC_ST_F1			(0x1fff<<16)


/*pwm register*/
#define PWM_CTRL 			(0x01a0)
#define v_PWM_EN(x)			(((x)&1)<<0)
#define v_PWM_MODE(x)		        (((x)&3)<<1)

#define v_DUTY_POL(x)		        (((x)&1)<<3)
#define v_INACTIVE_POL(x)		(((x)&1)<<4)
#define v_OUTPUT_MODE(x)		(((x)&1)<<5)
#define v_BL_EN(x)			(((x)&1)<<8)
#define v_CLK_SEL(x)		        (((x)&1)<<9)
#define v_PRESCALE(x)		        (((x)&7)<<12)
#define v_SCALE(x)			(((x)&0xff)<<16)
#define v_RPT(x)			(((x)&0xff)<<24)

#define m_PWM_EN			(1<<0)
#define m_PWM_MODE			(3<<1)

#define m_DUTY_POL			(1<<3)
#define m_INACTIVE_POL		        (1<<4)
#define m_OUTPUT_MODE		        (1<<5)
#define m_BL_EN			        (1<<8)
#define m_CLK_SEL			(1<<9)
#define m_PRESCALE			(7<<12)
#define m_SCALE		        	(0xff<<16)
#define m_RPT		           	((u32)0xff<<24)

#define PWM_PERIOD_HPR 			(0x01a4)
#define PWM_DUTY_LPR  			(0x01a8)
#define PWM_CNT 			(0x01ac)

/*BCSH register*/
#define BCSH_COLOR_BAR 			(0x01b0)
#define v_BCSH_EN(x)			(((x)&1)<<0)
#define v_BCSH_COLOR_BAR_Y(x)		(((x)&0x3ff)<<2)
#define v_BCSH_COLOR_BAR_U(x)		(((x)&0x3ff)<<12)
#define v_BCSH_COLOR_BAR_V(x)		(((x)&0x3ff)<<22)

#define m_BCSH_EN			(1<<0)
#define m_BCSH_COLOR_BAR_Y		(0x3ff<<2)
#define m_BCSH_COLOR_BAR_U		(0x3ff<<12)
#define m_BCSH_COLOR_BAR_V		((u32)0x3ff<<22)

#define BCSH_BCS 			(0x01b4)
#define v_BCSH_BRIGHTNESS(x)		(((x)&0xff)<<0)	
#define v_BCSH_CONTRAST(x)		(((x)&0x1ff)<<8)	
#define v_BCSH_SAT_CON(x)		(((x)&0x3ff)<<20)	
#define v_BCSH_OUT_MODE(x)		(((x)&0x3)<<30)	

#define m_BCSH_BRIGHTNESS		(0xff<<0)	
#define m_BCSH_CONTRAST			(0x1ff<<8)
#define m_BCSH_SAT_CON			(0x3ff<<20)	
#define m_BCSH_OUT_MODE			((u32)0x3<<30)	


#define BCSH_H 				(0x01b8) 
#define v_BCSH_SIN_HUE(x)		(((x)&0x1ff)<<0)
#define v_BCSH_COS_HUE(x)		(((x)&0x1ff)<<16)

#define m_BCSH_SIN_HUE			(0x1ff<<0)
#define m_BCSH_COS_HUE			(0x1ff<<16)

#define BCSH_CTRL			(0x01bc)
#define v_BCSH_Y2R_EN(x)		(((x)&0x1)<<0)
#define v_BCSH_R2Y_EN(x)		(((x)&0x1)<<4)
#define m_BCSH_Y2R_EN			(0x1<<0)
#define m_BCSH_R2Y_EN			(0x1<<4)

#define CABC_CTRL0			(0x01c0)
#define v_CABC_EN(x)				(((x)&1)<<0)
#define v_CABC_HANDLE_EN(x)			(((x)&1)<<1)
#define v_PWM_CONFIG_MODE(x)			(((x)&3)<<2)
#define v_CABC_CALC_PIXEL_NUM(x)		(((x)&0x7fffff)<<4)
#define m_CABC_EN				(1<<0)
#define m_CABC_HANDLE_EN			(1<<1)
#define m_PWM_CONFIG_MODE			(3<<2)
#define m_CABC_CALC_PIXEL_NUM			(0x7fffff<<4)

#define CABC_CTRL1			(0x01c4)
#define v_CABC_LUT_EN(x)			(((x)&1)<<0)
#define v_CABC_TOTAL_PIXEL_NUM(x)		(((x)&0x7fffff)<<4)
#define m_CABC_LUT_EN				(1<<0)
#define m_CABC_TOTAL_PIXEL_NUM			(0x7fffff<<4)

#define CABC_GAUSS_LINE0_0 		(0x01c8)
#define CABC_GAUSS_LINE0_1 		(0x01cc)
#define CABC_GAUSS_LINE1_0 		(0x01d0)
#define CABC_GAUSS_LINE1_1 		(0x01d4)
#define CABC_GAUSS_LINE2_0 		(0x01d8)
#define CABC_GAUSS_LINE2_1 		(0x01dc)

/*FRC register*/
#define FRC_LOWER01_0 			(0x01e0)
#define FRC_LOWER01_1 			(0x01e4)
#define FRC_LOWER10_0 			(0x01e8)
#define FRC_LOWER10_1 			(0x01ec)
#define FRC_LOWER11_0 			(0x01f0)
#define FRC_LOWER11_1 			(0x01f4)

#define CABC_CTRL2			(0x01f8)
#define v_CABC_STAGE_DOWN(x)			(((x)&0xff)<<0)
#define v_CABC_STAGE_UP(x)			(((x)&0x1ff)<<8)
#define v_CABC_STAGE_MODE(x)			(((x)&1)<<19)
#define v_MAX_SCALE_CFG_VALUE(x)		(((x)&0x1ff)<<20)
#define v_MAX_SCALE_CFG_ENABLE(x)		(((x)&1)<<31)
#define m_CABC_STAGE_DOWN			(0xff<<0)
#define m_CABC_STAGE_UP				(0x1ff<<8)
#define m_CABC_STAGE_MODE			(1<<19)
#define m_MAX_SCALE_CFG_VALUE			(0x1ff<<20)
#define m_MAX_SCALE_CFG_ENABLE			(1<<31)

#define CABC_CTRL3			(0x01fc)
#define v_CABC_GLOBAL_DN(x)			(((x)&0xff)<<0)
#define v_CABC_GLOBAL_DN_LIMIT_EN(x)		(((x)&1)<<8)
#define m_CABC_GLOBAL_DN			(0xff<<0)
#define m_CABC_GLOBAL_DN_LIMIT_EN		(1<<8)

#define MMU_DTE_ADDR			(0x0300)
#define v_MMU_DTE_ADDR(x)		(((x)&0xffffffff)<<0)
#define m_MMU_DTE_ADDR			(0xffffffff<<0)

#define MMU_STATUS			(0x0304)
#define v_PAGING_ENABLED(x)		(((x)&1)<<0)
#define v_PAGE_FAULT_ACTIVE(x)		(((x)&1)<<1)
#define v_STAIL_ACTIVE(x)		(((x)&1)<<2)
#define v_MMU_IDLE(x)			(((x)&1)<<3)
#define v_REPLAY_BUFFER_EMPTY(x)	(((x)&1)<<4)
#define v_PAGE_FAULT_IS_WRITE(x)	(((x)&1)<<5)
#define v_PAGE_FAULT_BUS_ID(x)		(((x)&0x1f)<<6)
#define m_PAGING_ENABLED		(1<<0)
#define m_PAGE_FAULT_ACTIVE		(1<<1)
#define m_STAIL_ACTIVE			(1<<2)
#define m_MMU_IDLE			(1<<3)
#define m_REPLAY_BUFFER_EMPTY		(1<<4)
#define m_PAGE_FAULT_IS_WRITE		(1<<5)
#define m_PAGE_FAULT_BUS_ID		(0x1f<<6)

#define MMU_COMMAND			(0x0308)
#define v_MMU_CMD(x)			(((x)&0x3)<<0)
#define m_MMU_CMD			(0x3<<0)

#define MMU_PAGE_FAULT_ADDR		(0x030c)
#define v_PAGE_FAULT_ADDR(x)		(((x)&0xffffffff)<<0)
#define m_PAGE_FAULT_ADDR		(0xffffffff<<0)

#define MMU_ZAP_ONE_LINE		(0x0310)
#define v_MMU_ZAP_ONE_LINE(x)		(((x)&0xffffffff)<<0)
#define m_MMU_ZAP_ONE_LINE		(0xffffffff<<0)

#define MMU_INT_RAWSTAT			(0x0314)
#define v_PAGE_FAULT_RAWSTAT(x)		(((x)&1)<<0)
#define v_READ_BUS_ERROR_RAWSTAT(x)	(((x)&1)<<1)
#define m_PAGE_FAULT_RAWSTAT		(1<<0)
#define m_READ_BUS_ERROR_RAWSTAT	(1<<1)

#define MMU_INT_CLEAR			(0x0318)
#define v_PAGE_FAULT_CLEAR(x)		(((x)&1)<<0)
#define v_READ_BUS_ERROR_CLEAR(x)	(((x)&1)<<1)
#define m_PAGE_FAULT_CLEAR		(1<<0)
#define m_READ_BUS_ERROR_CLEAR		(1<<1)

#define MMU_INT_MASK			(0x031c)
#define v_PAGE_FAULT_MASK(x)		(((x)&1)<<0)
#define v_READ_BUS_ERROR_MASK(x)	(((x)&1)<<1)
#define m_PAGE_FAULT_MASK		(1<<0)
#define m_READ_BUS_ERROR_MASK		(1<<1)

#define MMU_INT_STATUS			(0x0320)
#define v_PAGE_FAULT_STATUS(x)		(((x)&1)<<0)
#define v_READ_BUS_ERROR_STATUS(x)	(((x)&1)<<1)
#define m_PAGE_FAULT_STATUS		(1<<0)
#define m_READ_BUS_ERROR_STATUS		(1<<1)

#define MMU_AUTO_GATING			(0x0324)
#define v_MMU_AUTO_GATING(x)		(((x)&1)<<0)
#define m_MMU_AUTO_GATING		(1<<0)

#define WIN2_LUT_ADDR 			(0x0400)
#define WIN3_LUT_ADDR  			(0x0800)
#define HWC_LUT_ADDR   			(0x0c00)
#define GAMMA_LUT_ADDR 			(0x1000)
#define CABC_LUT_ADDR			(0x2000)
#define MCU_BYPASS_WPORT 		(0x2200) 
#define MCU_BYPASS_RPORT 		(0x2300)

#define PWM_MODE_ONE_SHOT		(0x0)
#define PWM_MODE_CONTINUOUS		(0x1)
#define PWM_MODE_CAPTURE		(0x2)
enum lb_mode {
    LB_YUV_3840X5 = 0x0,
    LB_YUV_2560X8 = 0x1,
    LB_RGB_3840X2 = 0x2,
    LB_RGB_2560X4 = 0x3,
    LB_RGB_1920X5 = 0x4,
    LB_RGB_1280X8 = 0x5 
};

enum sacle_up_mode {
    SCALE_UP_BIL = 0x0,
    SCALE_UP_BIC = 0x1
};

enum scale_down_mode {
    SCALE_DOWN_BIL = 0x0,
    SCALE_DOWN_AVG = 0x1
};

/*ALPHA BLENDING MODE*/
enum alpha_mode {               /*  Fs       Fd */
	AB_USER_DEFINE     = 0x0,
	AB_CLEAR    	   = 0x1,/*  0          0*/
	AB_SRC      	   = 0x2,/*  1          0*/
	AB_DST    	   = 0x3,/*  0          1  */
	AB_SRC_OVER   	   = 0x4,/*  1   	    1-As''*/
	AB_DST_OVER    	   = 0x5,/*  1-Ad''   1*/
	AB_SRC_IN    	   = 0x6,
	AB_DST_IN    	   = 0x7,
	AB_SRC_OUT    	   = 0x8,
	AB_DST_OUT    	   = 0x9,
	AB_SRC_ATOP        = 0xa,
	AB_DST_ATOP    	   = 0xb,
	XOR                = 0xc,
	AB_SRC_OVER_GLOBAL = 0xd
}; /*alpha_blending_mode*/

enum src_alpha_mode {
	AA_STRAIGHT	   = 0x0,
	AA_INVERSE         = 0x1
};/*src_alpha_mode*/

enum global_alpha_mode {
	AA_GLOBAL 	  = 0x0,
	AA_PER_PIX        = 0x1,
	AA_PER_PIX_GLOBAL = 0x2
};/*src_global_alpha_mode*/

enum src_alpha_sel {
	AA_SAT		= 0x0,
	AA_NO_SAT	= 0x1
};/*src_alpha_sel*/

enum src_color_mode {
	AA_SRC_PRE_MUL	       = 0x0,
	AA_SRC_NO_PRE_MUL      = 0x1
};/*src_color_mode*/

enum factor_mode {
	AA_ZERO			= 0x0,
	AA_ONE   	   	= 0x1,
	AA_SRC			= 0x2,
	AA_SRC_INVERSE          = 0x3,
	AA_SRC_GLOBAL           = 0x4
};/*src_factor_mode  &&  dst_factor_mode*/

enum cabc_stage_mode {
	LAST_FRAME_PWM_VAL	= 0x0,
	CUR_FRAME_PWM_VAL	= 0x1,
	STAGE_BY_STAGE		= 0x2
};

struct lcdc_device{
	int id;
	struct rk_lcdc_driver driver;
	struct device *dev;
	struct rk_screen *screen;

	void __iomem *regs;
	void *regsbak;			/*back up reg*/
	u32 reg_phy_base;       	/* physical basic address of lcdc register*/
	u32 len;               		/* physical map length of lcdc register*/
	spinlock_t  reg_lock;		/*one time only one process allowed to config the register*/
	
	int __iomem *dsp_lut_addr_base;


	int prop;			/*used for primary or extended display device*/
	bool pre_init;
	bool pwr18;			/*if lcdc use 1.8v power supply*/
	bool clk_on;			/*if aclk or hclk is closed ,acess to register is not allowed*/
	u8 atv_layer_cnt;		/*active layer counter,when  atv_layer_cnt = 0,disable lcdc*/
	

	unsigned int		irq;

	struct clk		*pd;				/*lcdc power domain*/
	struct clk		*hclk;				/*lcdc AHP clk*/
	struct clk		*dclk;				/*lcdc dclk*/
	struct clk		*aclk;				/*lcdc share memory frequency*/
	u32 pixclock;	

	u32 standby;						/*1:standby,0:wrok*/
	u32 iommu_status;
};

struct alpha_config{
	enum src_alpha_mode src_alpha_mode;       /*win0_src_alpha_m0*/
	u32 src_global_alpha_val; /*win0_src_global_alpha*/
	enum global_alpha_mode src_global_alpha_mode;/*win0_src_blend_m0*/
	enum src_alpha_sel src_alpha_cal_m0;	 /*win0_src_alpha_cal_m0*/
	enum src_color_mode src_color_mode;	 /*win0_src_color_m0*/
	enum factor_mode src_factor_mode;	 /*win0_src_factor_m0*/
	enum factor_mode dst_factor_mode;      /*win0_dst_factor_m0*/
};

struct lcdc_cabc_mode {
	u32 pixel_num;			/* pixel precent number */
	u16 stage_up;			/* up stride */
	u16 stage_down;		/* down stride */
};

static inline void lcdc_writel(struct lcdc_device *lcdc_dev,u32 offset,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);	
	*_pv = v;
	writel_relaxed(v,lcdc_dev->regs+offset);	
}

static inline u32 lcdc_readl(struct lcdc_device *lcdc_dev,u32 offset)
{
	u32 v;
	v = readl_relaxed(lcdc_dev->regs+offset);
	return v;
}

static inline u32 lcdc_read_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;
	u32 _v = readl_relaxed(lcdc_dev->regs+offset); 
	_pv += (offset >> 2);
	*_pv = _v;
       _v &= msk;
       return (_v?1:0);   
}

static inline void  lcdc_set_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) |= msk;				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void lcdc_clr_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk)
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) &= (~msk);				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void  lcdc_msk_reg(struct lcdc_device *lcdc_dev,u32 offset,u32 msk,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);			
	(*_pv) &= (~msk);				
	(*_pv) |= v;				
	writel_relaxed(*_pv,lcdc_dev->regs+offset);	
}

static inline void lcdc_cfg_done(struct lcdc_device *lcdc_dev) 
{
	writel_relaxed(0x01,lcdc_dev->regs+REG_CFG_DONE); 
	dsb();	
} 

#define CUBIC_PRECISE  0
#define CUBIC_SPLINE   1
#define CUBIC_CATROM   2
#define CUBIC_MITCHELL 3

#define CUBIC_MODE_SELETION      CUBIC_PRECISE

/*****************************************************************************************************/
#define SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT   12   /* 4.12*/
#define SCALE_FACTOR_BILI_DN_FIXPOINT(x)      ((INT32)((x)*(1 << SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_BILI_UP_FIXPOINT_SHIFT   16   /* 0.16*/

#define SCALE_FACTOR_AVRG_FIXPOINT_SHIFT   16   /*0.16*/
#define SCALE_FACTOR_AVRG_FIXPOINT(x)      ((INT32)((x)*(1 << SCALE_FACTOR_AVRG_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_BIC_FIXPOINT_SHIFT    16   /* 0.16*/
#define SCALE_FACTOR_BIC_FIXPOINT(x)       ((INT32)((x)*(1 << SCALE_FACTOR_BIC_FIXPOINT_SHIFT)))

#define SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT    12  /*NONE SCALE,vsd_bil*/
#define SCALE_FACTOR_VSDBIL_FIXPOINT_SHIFT     12  /*VER SCALE DOWN BIL*/

/*****************************************************************************************************/

/*#define GET_SCALE_FACTOR_BILI(src, dst) ((((src) - 1) << SCALE_FACTOR_BILI_FIXPOINT_SHIFT) / ((dst) - 1))*/
/*#define GET_SCALE_FACTOR_BIC(src, dst)  ((((src) - 1) << SCALE_FACTOR_BIC_FIXPOINT_SHIFT) / ((dst) - 1))*/
/*modified by hpz*/
#define GET_SCALE_FACTOR_BILI_DN(src, dst)  ((((src)*2 - 3) << (SCALE_FACTOR_BILI_DN_FIXPOINT_SHIFT-1)) / ((dst) - 1))
#define GET_SCALE_FACTOR_BILI_UP(src, dst)  ((((src)*2 - 3) << (SCALE_FACTOR_BILI_UP_FIXPOINT_SHIFT-1)) / ((dst) - 1))
#define GET_SCALE_FACTOR_BIC(src, dst)      ((((src)*2 - 3) << (SCALE_FACTOR_BIC_FIXPOINT_SHIFT-1)) / ((dst) - 1))

/*****************************************************************/
/*NOTE: hardware为节省开销, srcH先抽行得到 (srcH+vScaleDnMult-1)/vScaleDnMult; 然后缩放*/
#define GET_SCALE_DN_ACT_HEIGHT(srcH, vScaleDnMult) (((srcH)+(vScaleDnMult)-1)/(vScaleDnMult))

/*#define VSKIP_MORE_PRECISE*/

#ifdef VSKIP_MORE_PRECISE
#define MIN_SCALE_FACTOR_AFTER_VSKIP        1.5f
#define GET_SCALE_FACTOR_BILI_DN_VSKIP(srcH, dstH, vScaleDnMult) \
            (GET_SCALE_FACTOR_BILI_DN(GET_SCALE_DN_ACT_HEIGHT((srcH), (vScaleDnMult)), (dstH)))
#else
#define MIN_SCALE_FACTOR_AFTER_VSKIP        1
#define GET_SCALE_FACTOR_BILI_DN_VSKIP(srcH, dstH, vScaleDnMult) \
            ((GET_SCALE_DN_ACT_HEIGHT((srcH), (vScaleDnMult)) == (dstH))\
                            ? (GET_SCALE_FACTOR_BILI_DN((srcH), (dstH))/(vScaleDnMult))\
                            : GET_SCALE_FACTOR_BILI_DN(GET_SCALE_DN_ACT_HEIGHT((srcH), (vScaleDnMult)), (dstH)))
#endif
/*****************************************************************/


/*ScaleFactor must >= dst/src, or pixels at end of line may be unused*/
/*ScaleFactor must < dst/(src-1), or dst buffer may overflow*/
/*avrg old code:       ((((dst) << SCALE_FACTOR_AVRG_FIXPOINT_SHIFT))/((src) - 1)) hxx_chgsrc*/
/*modified by hpz:*/
#define GET_SCALE_FACTOR_AVRG(src, dst)  ((((dst) << (SCALE_FACTOR_AVRG_FIXPOINT_SHIFT+1)))/(2*(src) - 1))

/*****************************************************************************************************/
/*Scale Coordinate Accumulate, x.16*/
#define SCALE_COOR_ACC_FIXPOINT_SHIFT     16
#define SCALE_COOR_ACC_FIXPOINT_ONE       (1 << SCALE_COOR_ACC_FIXPOINT_SHIFT)
#define SCALE_COOR_ACC_FIXPOINT(x)        ((INT32)((x)*(1 << SCALE_COOR_ACC_FIXPOINT_SHIFT)))
#define SCALE_COOR_ACC_FIXPOINT_REVERT(x) ((((x) >> (SCALE_COOR_ACC_FIXPOINT_SHIFT-1)) + 1) >> 1)

#define SCALE_GET_COOR_ACC_FIXPOINT(scaleFactor, factorFixpointShift)  \
        ((scaleFactor) << (SCALE_COOR_ACC_FIXPOINT_SHIFT - (factorFixpointShift)))


/*****************************************************************************************************/
/*CoarsePart of Scale Coordinate Accumulate, used for pixel mult-add factor, 0.8*/
#define SCALE_FILTER_FACTOR_FIXPOINT_SHIFT     8
#define SCALE_FILTER_FACTOR_FIXPOINT_ONE       (1 << SCALE_FILTER_FACTOR_FIXPOINT_SHIFT)
#define SCALE_FILTER_FACTOR_FIXPOINT(x)        ((INT32)((x)*(1 << SCALE_FILTER_FACTOR_FIXPOINT_SHIFT)))
#define SCALE_FILTER_FACTOR_FIXPOINT_REVERT(x) ((((x) >> (SCALE_FILTER_FACTOR_FIXPOINT_SHIFT-1)) + 1) >> 1)

#define SCALE_GET_FILTER_FACTOR_FIXPOINT(coorAccumulate, coorAccFixpointShift) \
  (((coorAccumulate)>>((coorAccFixpointShift)-SCALE_FILTER_FACTOR_FIXPOINT_SHIFT))&(SCALE_FILTER_FACTOR_FIXPOINT_ONE-1))

#define SCALE_OFFSET_FIXPOINT_SHIFT            8
#define SCALE_OFFSET_FIXPOINT(x)              ((INT32)((x)*(1 << SCALE_OFFSET_FIXPOINT_SHIFT)))

u32 getHardWareVSkipLines(u32 srcH, u32 dstH)
{
    u32 vScaleDnMult;

    if(srcH >= (u32)(4*dstH*MIN_SCALE_FACTOR_AFTER_VSKIP))
    {
        vScaleDnMult = 4;
    }
    else if(srcH >= (u32)(2*dstH*MIN_SCALE_FACTOR_AFTER_VSKIP))
    {
        vScaleDnMult = 2;
    }
    else
    {
        vScaleDnMult = 1;
    }

    return vScaleDnMult;
}
#endif
