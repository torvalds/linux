 #ifndef __DE_BSP_I_H__
#define __DE_BSP_I_H__


#include "ebios_de.h"

/*front-end registers offset*/
#define DE_SCAL_EN_OFF                        0x000	/*front-end enable register offset*/
#define DE_SCAL_FRM_CTL_OFF                   0x004	/*front-end frame process control  register offset*/
#define DE_SCAL_SCL_CSC_BYPASS_OFF            0x008	/*scaler/csc0 bypass control register offset*/
#define DE_SCAL_SCL_ALGSEL_OFF                0x00C	/*scaling algorithm selection register offset*/
#define DE_SCAL_INIMGSIZE_OFF                 0x010	/*input image size register offset*/
#define DE_SCAL_OUTLAYSIZE_OFF                0x014	/*output layer size register offset*/
#define DE_SCAL_SCL_HFACTOR_OFF               0x018	/*scaler horizontal scaling factor register offset*/
#define DE_SCAL_SCL_VFACTOR_OFF               0x01C	/*scaler vertical scaling factor register offset*/
#define DE_SCAL_FRMBUF_BASE_ADDR_OFF          0x020	/*channel0 frame buffer base address register offset*/
#define DE_SCAL_FRMBUF_MBLK_OFF_OFF           0x030	/*channel0 frame buffer macro block offset register offset*/
#define DE_SCAL_FRMBUF_LINESTRIDE_OFF         0x040	/*channel0 frame buffer line stride register offset*/
#define DE_SCAL_INDATA_FMT_OFF                0x04C	/*front-end input data format register offset*/
#define DE_SCAL_WBACK_ADDR_OFF                0x050	/*front-end write back address register offset*/
#define DE_SCAL_OUTDATA_FMT_OFF               0x05C	/*front-end output data format register offset*/
#define DE_SCAL_INTE_EN_OFF                   0x060	/*front-end interrupt enable register offset*/
#define DE_SCAL_INTE_STS_OFF                  0x064	/*front-end interrupt status register offset*/
#define DE_SCAL_STS_OFF                       0x068	/*front-end status register offset*/
#define DE_SCAL_CSC0_YRCFT_OFF                0x070	/*CSC0 YR coefficient register offset*/
#define DE_SCAL_CSC0_YGCFT_OFF                0x074	/*CSC0 YG coefficient register offset*/
#define DE_SCAL_CSC0_YBCFT_OFF                0x078	/*CSC0 YB coefficient register offset*/
#define DE_SCAL_CSC0_YCST_OFF                 0x07C	/*CSC0 Y constant register offset*/
#define DE_SCAL_CSC0_URCFT_OFF                0x080	/*CSC0 UR coefficient register offset*/
#define DE_SCAL_CSC0_UGCFT_OFF                0x084	/*CSC0 UG coefficient register offset*/
#define DE_SCAL_CSC0_UBCFT_OFF                0x088	/*CSC0 UB coefficient register offset*/
#define DE_SCAL_CSC0_UCST_OFF                 0x08C	/*CSC0 U constant register offset*/
#define DE_SCAL_CSC0_VRCFT_OFF                0x090	/*CSC0 VR coefficient register offset*/
#define DE_SCAL_CSC0_VGCFT_OFF                0x094	/*CSC0 VG coefficient register offset*/
#define DE_SCAL_CSC0_VBCFT_OFF                0x098	/*CSC0 VB coefficient register offset*/
#define DE_SCAL_CSC0_VCST_OFF                 0x09C	/*CSC0 V constant register offset*/
#define DE_SCAL_CSC0_RYCFT_OFF                0x080	/*CSC0 RY coefficient register offset*/
#define DE_SCAL_CSC0_RUCFT_OFF                0x084	/*CSC0 RU coefficient register offset*/
#define DE_SCAL_CSC0_RVCFT_OFF                0x088	/*CSC0 RV coefficient register offset*/
#define DE_SCAL_CSC0_RCST_OFF                 0x08C	/*CSC0 R constant register offset*/
#define DE_SCAL_CSC0_GYCFT_OFF                0x070	/*CSC0 GY coefficient register offset*/
#define DE_SCAL_CSC0_GUCFT_OFF                0x074	/*CSC0 GU coefficient register offset*/
#define DE_SCAL_CSC0_GVCFT_OFF                0x078	/*CSC0 GV coefficient register offset*/
#define DE_SCAL_CSC0_GCST_OFF                 0x07C	/*CSC0 G constant register offset*/
#define DE_SCAL_CSC0_BYCFT_OFF                0x090	/*CSC0 BY coefficient register offset*/
#define DE_SCAL_CSC0_BUCFT_OFF                0x094	/*CSC0 BU coefficient register offset*/
#define DE_SCAL_CSC0_BVCFT_OFF                0x098	/*CSC0 BV coefficient register offset*/
#define DE_SCAL_CSC0_BCST_OFF                 0x09C	/*CSC0 B constant register offset*/
#define DE_SCAL_SCL_HFTLCFT_RBLK_OFF0         0x200	/*scaler horizontal filter coefficient  RAM block register offset*/
#define DE_SCAL_SCL_VFTLCFT_RBLK_OFF          0x300	/*scaler verifical filter coefficient  RAM block register offset*/


/*back-end registers offset*/
#define DE_BE_MODE_CTL_OFF  		            0x800	/*back-end mode control register offset*/
#define DE_BE_COLOR_CTL_OFF   		            0x804	/*back-end color control register offset*/
#define DE_BE_LAYER_SIZE_OFF  		            0x810	/*back-end layer size register offset*/
#define DE_BE_LAYER_CRD_CTL_OFF  	            0x820	/*back-end layer coordinate control register offset*/
#define DE_BE_FRMBUF_WLINE_OFF   	            0x840	/*back-end frame buffer line width register offset*/
#define DE_BE_FRMBUFA_ADDR_OFF  	            0X850	/*back-end frame buffera  address  register offset*/
#define DE_BE_FRMBUFB_ADDR_OFF  	            0X860	/*back-end frame bufferb addreass  register offset*/
#define DE_BE_FRMBUF_CTL_OFF  		            0X870	/*back-end frame buffer control register offset*/
#define DE_BE_CLRKEY_MAX_OFF   	                0x880	/*back-end color key max register offset*/
#define DE_BE_CLRKEY_MIN_OFF  		            0x884	/*back-end color key min register offset*/
#define DE_BE_CLRKEY_CFG_OFF   		            0x888	/*back-end color key configuration register offset*/
#define DE_BE_LAYER_ATTRCTL_OFF0  	            0x890	/*back-end layer attribute control register0 offset*/
#define DE_BE_LAYER_ATTRCTL_OFF1  	            0x8a0	/*back-end layer attribute control register1 offset*/
#define DE_BE_DLCDP_CTL_OFF  		            0x8b0	/*direct lcd pipe control register offset*/
#define DE_BE_DLCDP_FRMBUF_ADDRCTL_OFF          0x8b4	/*direct lcd pipe frame buffer address control  register offset*/
#define DE_BE_DLCDP_CRD_CTL_OFF0                0x8b8	/*direct lcd pipe coordinate control  register0 offset*/
#define DE_BE_DLCDP_CRD_CTL_OFF1                0x8bc	/*direct lcd pipe coordinate control register1 offset*/
#define DE_BE_INT_EN_OFF                        0x8c0
#define DE_BE_INT_FLAG_OFF                      0x8c4
#define DE_BE_HWC_CRD_CTL_OFF             	    0x8d8	/*hardware cursor coordinate control register offset*/
#define DE_BE_HWC_FRMBUF_OFF                    0x8e0	/*hardware cursor framebuffer control*/
#define DE_BE_WB_CTRL_OFF						0x8f0	/*back-end write back control */
#define DE_BE_WB_ADDR_OFF						0x8f4	/*back-end write back address*/
#define DE_BE_WB_LINE_WIDTH_OFF					0x8f8	/*back-end write back buffer line width*/
#define DE_BE_SPRITE_EN_OFF						0x900	/*sprite enable*/
#define DE_BE_SPRITE_FORMAT_CTRL_OFF			0x908	/*sprite format control*/
#define DE_BE_SPRITE_ALPHA_CTRL_OFF				0x90c	/*sprite alpha ctrol*/
#define DE_BE_SPRITE_POS_CTRL_OFF				0xa00	/*sprite single block coordinate control*/
#define DE_BE_SPRITE_ATTR_CTRL_OFF				0xb00	/*sprite single block attribute control*/
#define DE_BE_SPRITE_ADDR_OFF					0xc00	/*sprite single block address setting SRAM array*/
#define DE_BE_SPRITE_LINE_WIDTH_OFF             0xd00
#define DE_BE_YUV_CTRL_OFF						0x920	/*back-end input YUV channel control*/
#define DE_BE_YUV_ADDR_OFF						0x930	/*back-end YUV channel frame buffer address*/
#define DE_BE_YUV_LINE_WIDTH_OFF				0x940	/*back-end YUV channel buffer line width*/
#define DE_BE_YG_COEFF_OFF						0x950	/*back Y/G coefficient*/
#define DE_BE_YG_CONSTANT_OFF					0x95c	/*back Y/G constant*/
#define DE_BE_UR_COEFF_OFF						0x960	/*back U/R coefficient*/
#define DE_BE_UR_CONSTANT_OFF					0x96c	/*back U/R constant*/
#define DE_BE_VB_COEFF_OFF						0x970	/*back V/B coefficient*/
#define DE_BE_VB_CONSTANT_OFF					0x97c	/*back V/B constant*/
#define DE_BE_OUT_COLOR_CTRL_OFF                0x9c0
#define DE_BE_OUT_COLOR_R_COEFF_OFF             0x9d0
#define DE_BE_OUT_COLOR_R_CONSTANT_OFF          0x9dc
#define DE_BE_OUT_COLOR_G_COEFF_OFF             0x9e0
#define DE_BE_OUT_COLOR_G_CONSTANT_OFF          0x9ec
#define DE_BE_OUT_COLOR_B_COEFF_OFF             0x9f0
#define DE_BE_OUT_COLOR_B_CONSTANT_OFF          0x9fc

#define DE_BE_REG_ADDR_OFF                      0x0

#define DE_BE_HWC_PALETTE_TABLE_ADDR_OFF        0x1000	/*back-end hardware cursor palette table address*/
#define DE_BE_INTER_PALETTE_TABLE_ADDR_OFF      0x1400	/*back-end internal framebuffer or direct lcd pipe palette table*/
#define DE_BE_SPRITE_PALETTE_TABLE_ADDR_OFF		0x1800	/*back-end sprite palette table address*/
#define DE_BE_HWC_PATTERN_ADDR_OFF              0x2000	/*back-end hwc pattern memory block address*/
#define DE_BE_INTERNAL_FB_ADDR_OFF              0x3000	/*back-end internal frame bufffer address definition*/
#define DE_BE_GAMMA_TABLE_ADDR_OFF              0x3000	/*back-end gamma table address*/
#define DE_BE_PALETTE_TABLE_ADDR_OFF            0x3400	/*back-end palette table address*/
#define DE_FE_REG_ADDR_OFF                      0x20000
#define DE_SCAL2_REG_ADDR_OFF                   0x40000

#define DE_BE_REG_SIZE                      0x1000
#define DE_BE_HWC_PALETTE_TABLE_SIZE        0x400	/*back-end hardware cursor palette table size*/
#define DE_BE_INTER_PALETTE_TABLE_SIZE      0x400	/*back-end internal framebuffer or direct lcd pipe palette table size in bytes*/
#define DE_BE_SPRITE_PALETTE_TABLE_SIZE		0x400	/*back-end sprite palette table size in bytes*/
#define DE_BE_HWC_PATTERN_SIZE              0x400
#define DE_BE_INTERNAL_FB_SIZE              0x800	/**back-end internal frame buffer size in byte*/
#define DE_BE_GAMMA_TABLE_SIZE              0x400	/*back-end gamma table size*/
#define DE_BE_PALETTE_TABLE_SIZE            0x400	/*back-end palette table size in bytes*/
#define DE_FE_REG_SIZE                      0x1000
#define DE_SCAL2_REG_SIZE                   0x1000


extern __u32 image0_reg_base;
#define DE_SCAL_GET_REG_BASE(sel)    ((sel)==0?(image0_reg_base+DE_FE_REG_ADDR_OFF):(image0_reg_base+DE_SCAL2_REG_ADDR_OFF))
#define DE_BE_GET_REG_BASE()    (image0_reg_base+DE_BE_REG_ADDR_OFF)


#define DE_WUINT8(offset,value)             (*((volatile __u8 *)(offset))=(value))
#define DE_RUINT8(offset)                   (*((volatile __u8 *)(offset)))
#define DE_WUINT16(offset,value)            (*((volatile __u16 *)(offset))=(value))
#define DE_RUINT16(offset)                  (*((volatile __u16 *)(offset)))
#define DE_WUINT32(offset,value)            (*((volatile __u32 *)(offset))=(value))
#define DE_RUINT32(offset)                  (*((volatile __u32 *)(offset)))
#define DE_WUINT8IDX(offset,index,value)    ((*((volatile __u8 *)(offset+index)))=(value))
#define DE_RUINT8IDX(offset,index)          (*((volatile __u8 *)(offset+index)))
#define DE_WUINT16IDX(offset,index,value)   (*((volatile __u16 *)(offset+2*index))=(value))
#define DE_RUINT16IDX(offset,index)         ( *((volatile __u16 *)(offset+2*index)))
#define DE_WUINT32IDX(offset,index,value)   (*((volatile __u32 *)(offset+4*index))=(value))
#define DE_RUINT32IDX(offset,index)         (*((volatile __u32 *)(offset+4*index)))

#define DE_SCAL_WUINT8(sel,offset,value)        DE_WUINT8(DE_SCAL_GET_REG_BASE(sel)+(offset),value)
#define DE_SCAL_RUINT8(sel,offset)              DE_RUINT8(DE_SCAL_GET_REG_BASE(sel)+(offset))
#define DE_SCAL_WUINT16(sel,offset,value)       DE_WUINT16(DE_SCAL_GET_REG_BASE(sel)+(offset),value)
#define DE_SCAL_RUINT16(sel,offset)             DE_RUINT16(DE_SCAL_GET_REG_BASE(sel)+(offset))
#define DE_SCAL_WUINT32(sel,offset,value)       DE_WUINT32(DE_SCAL_GET_REG_BASE(sel)+(offset),value)
#define DE_SCAL_RUINT32(sel,offset)             DE_RUINT32(DE_SCAL_GET_REG_BASE(sel)+(offset))
#define DE_SCAL_WUINT8IDX(sel,offset,index,value)  DE_WUINT8IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index,value)
#define DE_SCAL_RUINT8IDX(sel,offset,index)        DE_RUINT8IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index)
#define DE_SCAL_WUINT16IDX(sel,offset,index,value) DE_WUINT16IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index,value)
#define DE_SCAL_RUINT16IDX(sel,offset,index)       DE_RUINT16IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index)
#define DE_SCAL_WUINT32IDX(sel,offset,index,value) DE_WUINT32IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index,value)
#define DE_SCAL_RUINT32IDX(sel,offset,index)       DE_RUINT32IDX(DE_SCAL_GET_REG_BASE(sel)+(offset),index)


#define DE_BE_WUINT8(offset,value)        DE_WUINT8(DE_BE_GET_REG_BASE()+(offset),value)
#define DE_BE_RUINT8(offset)              DE_RUINT8(DE_BE_GET_REG_BASE()+(offset))
#define DE_BE_WUINT16(offset,value)       DE_WUINT16(DE_BE_GET_REG_BASE()+(offset),value)
#define DE_BE_RUINT16(offset)             DE_RUINT16(DE_BE_GET_REG_BASE()+(offset))
#define DE_BE_WUINT32(offset,value)       DE_WUINT32(DE_BE_GET_REG_BASE()+(offset),value)
#define DE_BE_RUINT32(offset)             DE_RUINT32(DE_BE_GET_REG_BASE()+(offset))
#define DE_BE_WUINT8IDX(offset,index,value)  DE_WUINT8IDX(DE_BE_GET_REG_BASE()+(offset),index,value)
#define DE_BE_RUINT8IDX(offset,index)        DE_RUINT8IDX(DE_BE_GET_REG_BASE()+(offset),index)
#define DE_BE_WUINT16IDX(offset,index,value) DE_WUINT16IDX(DE_BE_GET_REG_BASE()+(offset),index,value)
#define DE_BE_RUINT16IDX(offset,index)       DE_RUINT16IDX(DE_BE_GET_REG_BASE()+(offset),index)
#define DE_BE_WUINT32IDX(offset,index,value) DE_WUINT32IDX(DE_BE_GET_REG_BASE()+(offset),index,value)
#define DE_BE_RUINT32IDX(offset,index)       DE_RUINT32IDX(DE_BE_GET_REG_BASE()+(offset),index)

extern __u32  csc_tab[192];
extern __u32  fir_tab[672];
extern __u32  image_enhance_tab[224];
#endif
