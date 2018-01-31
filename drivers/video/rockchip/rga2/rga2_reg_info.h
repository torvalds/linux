/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REG2_INFO_H__
#define __REG2_INFO_H__


//#include "chip_register.h"

//#include "rga_struct.h"
#include "rga2.h"

#ifndef MIN
#define MIN(X, Y)           ((X)<(Y)?(X):(Y))
#endif

#ifndef MAX
#define MAX(X, Y)           ((X)>(Y)?(X):(Y))
#endif

#ifndef ABS
#define ABS(X)              (((X) < 0) ? (-(X)) : (X))
#endif

#ifndef CLIP
#define CLIP(x, a,  b)				((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x))
#endif

#define rRGA_SYS_CTRL             (*(volatile u32 *)(RGA2_BASE + RGA2_SYS_CTRL_OFFSET    ))
#define rRGA_CMD_CTRL             (*(volatile u32 *)(RGA2_BASE + RGA2_CMD_CTRL_OFFSET    ))
#define rRGA_CMD_BASE             (*(volatile u32 *)(RGA2_BASE + RGA2_CMD_BASE_OFFSET    ))
#define rRGA_STATUS               (*(volatile u32 *)(RGA2_BASE + RGA2_STATUS_OFFSET      ))
#define rRGA_INT                  (*(volatile u32 *)(RGA2_BASE + RGA2_INT_OFFSET         ))
#define rRGA_MMU_CTRL0            (*(volatile u32 *)(RGA2_BASE + RGA2_MMU_CTRL0_OFFSET   ))
#define rRGA_MMU_CMD_BASE         (*(volatile u32 *)(RGA2_BASE + RGA2_MMU_CMD_BASE_OFFSET))
#define rRGA_CMD_ADDR             (*(volatile u32 *)(RGA2_BASE + RGA2_CMD_ADDR))

/*RGA_INT*/
#define m_RGA2_INT_ALL_CMD_DONE_INT_EN             ( 1<<10 )
#define m_RGA2_INT_MMU_INT_EN                      ( 1<<9  )
#define m_RGA2_INT_ERROR_INT_EN                    ( 1<<8  )
#define m_RGA2_INT_NOW_CMD_DONE_INT_CLEAR          ( 1<<7  )
#define m_RGA2_INT_ALL_CMD_DONE_INT_CLEAR          ( 1<<6  )
#define m_RGA2_INT_MMU_INT_CLEAR                   ( 1<<5  )
#define m_RGA2_INT_ERROR_INT_CLEAR                 ( 1<<4  )
#define m_RGA2_INT_CUR_CMD_DONE_INT_FLAG           ( 1<<3  )
#define m_RGA2_INT_ALL_CMD_DONE_INT_FLAG           ( 1<<2  )
#define m_RGA2_INT_MMU_INT_FLAG                    ( 1<<1  )
#define m_RGA2_INT_ERROR_INT_FLAG                  ( 1<<0  )

#define s_RGA2_INT_ALL_CMD_DONE_INT_EN(x)          ( (x&0x1)<<10 )
#define s_RGA2_INT_MMU_INT_EN(x)                   ( (x&0x1)<<9  )
#define s_RGA2_INT_ERROR_INT_EN(x)                 ( (x&0x1)<<8  )
#define s_RGA2_INT_NOW_CMD_DONE_INT_CLEAR(x)       ( (x&0x1)<<7  )
#define s_RGA2_INT_ALL_CMD_DONE_INT_CLEAR(x)       ( (x&0x1)<<6  )
#define s_RGA2_INT_MMU_INT_CLEAR(x)                ( (x&0x1)<<5  )
#define s_RGA2_INT_ERROR_INT_CLEAR(x)              ( (x&0x1)<<4  )



/* RGA_MODE_CTRL */
#define m_RGA2_MODE_CTRL_SW_RENDER_MODE         (  0x7<<0  )
#define m_RGA2_MODE_CTRL_SW_BITBLT_MODE         (  0x1<<3  )
#define m_RGA2_MODE_CTRL_SW_CF_ROP4_PAT         (  0x1<<4  )
#define m_RGA2_MODE_CTRL_SW_ALPHA_ZERO_KET      (  0x1<<5  )
#define m_RGA2_MODE_CTRL_SW_GRADIENT_SAT        (  0x1<<6  )
#define m_RGA2_MODE_CTRL_SW_INTR_CF_E           (  0x1<<7  )

#define s_RGA2_MODE_CTRL_SW_RENDER_MODE(x)      (  (x&0x7)<<0  )
#define s_RGA2_MODE_CTRL_SW_BITBLT_MODE(x)      (  (x&0x1)<<3  )
#define s_RGA2_MODE_CTRL_SW_CF_ROP4_PAT(x)      (  (x&0x1)<<4  )
#define s_RGA2_MODE_CTRL_SW_ALPHA_ZERO_KET(x)   (  (x&0x1)<<5  )
#define s_RGA2_MODE_CTRL_SW_GRADIENT_SAT(x)     (  (x&0x1)<<6  )
#define s_RGA2_MODE_CTRL_SW_INTR_CF_E(x)        (  (x&0x1)<<7  )

/* RGA_SRC_INFO */
#define m_RGA2_SRC_INFO_SW_SRC_FMT                (   0xf<<0   )
#define m_RGA2_SRC_INFO_SW_SW_SRC_RB_SWAP         (   0x1<<4   )
#define m_RGA2_SRC_INFO_SW_SW_SRC_ALPHA_SWAP      (   0x1<<5   )
#define m_RGA2_SRC_INFO_SW_SW_SRC_UV_SWAP         (   0x1<<6   )
#define m_RGA2_SRC_INFO_SW_SW_CP_ENDAIN           (   0x1<<7   )
#define m_RGA2_SRC_INFO_SW_SW_SRC_CSC_MODE        (   0x3<<8   )
#define m_RGA2_SRC_INFO_SW_SW_SRC_ROT_MODE        (   0x3<<10  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_MIR_MODE        (   0x3<<12  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_HSCL_MODE       (   0x3<<14  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_VSCL_MODE       (   0x3<<16  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_TRANS_MODE      (   0x1<<18  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_TRANS_E         (   0xf<<19  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_DITHER_UP_E     (   0x1<<23  )
#define m_RGA2_SRC_INFO_SW_SW_SRC_SCL_FILTER      (   0x3<<24  )
#define m_RGA2_SRC_INFO_SW_SW_VSP_MODE_SEL        (   0x1<<26  )
#define m_RGA2_SRC_INFO_SW_SW_YUV10_E             (   0x1<<27  )
#define m_RGA2_SRC_INFO_SW_SW_YUV10_ROUND_E       (   0x1<<28  )





#define s_RGA2_SRC_INFO_SW_SRC_FMT(x)                (   (x&0xf)<<0   )
#define s_RGA2_SRC_INFO_SW_SW_SRC_RB_SWAP(x)         (   (x&0x1)<<4   )
#define s_RGA2_SRC_INFO_SW_SW_SRC_ALPHA_SWAP(x)      (   (x&0x1)<<5   )
#define s_RGA2_SRC_INFO_SW_SW_SRC_UV_SWAP(x)         (   (x&0x1)<<6   )
#define s_RGA2_SRC_INFO_SW_SW_CP_ENDAIN(x)           (   (x&0x1)<<7   )
#define s_RGA2_SRC_INFO_SW_SW_SRC_CSC_MODE(x)        (   (x&0x3)<<8   )
#define s_RGA2_SRC_INFO_SW_SW_SRC_ROT_MODE(x)        (   (x&0x3)<<10  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_MIR_MODE(x)        (   (x&0x3)<<12  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_HSCL_MODE(x)       (   (x&0x3)<<14  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_VSCL_MODE(x)       (   (x&0x3)<<16  )

#define s_RGA2_SRC_INFO_SW_SW_SRC_TRANS_MODE(x)      (   (x&0x1)<<18  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_TRANS_E(x)         (   (x&0xf)<<19  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_DITHER_UP_E(x)     (   (x&0x1)<<23  )
#define s_RGA2_SRC_INFO_SW_SW_SRC_SCL_FILTER(x)      (   (x&0x3)<<24  )
#define s_RGA2_SRC_INFO_SW_SW_VSP_MODE_SEL(x)        (   (x&0x1)<<26  )
#define s_RGA2_SRC_INFO_SW_SW_YUV10_E(x)             (   (x&0x1)<<27  )
#define s_RGA2_SRC_INFO_SW_SW_YUV10_ROUND_E(x)       (   (x&0x1)<<28  )

/* RGA_SRC_VIR_INFO */
#define m_RGA2_SRC_VIR_INFO_SW_SRC_VIR_STRIDE        (  0x7fff<<0  )         //modify
#define m_RGA2_SRC_VIR_INFO_SW_MASK_VIR_STRIDE       (   0x3ff<<16 )         //modify

#define s_RGA2_SRC_VIR_INFO_SW_SRC_VIR_STRIDE(x)        ( (x&0x7fff)<<0  )   //modify
#define s_RGA2_SRC_VIR_INFO_SW_MASK_VIR_STRIDE(x)       (   (x&0x3ff)<<16 )  //modify


/* RGA_SRC_ACT_INFO */
#define m_RGA2_SRC_ACT_INFO_SW_SRC_ACT_WIDTH        (  0x1fff<<0  )
#define m_RGA2_SRC_ACT_INFO_SW_SRC_ACT_HEIGHT       (  0x1fff<<16  )

#define s_RGA2_SRC_ACT_INFO_SW_SRC_ACT_WIDTH(x)        (  (x&0x1fff)<<0  )
#define s_RGA2_SRC_ACT_INFO_SW_SRC_ACT_HEIGHT(x)       (  (x&0x1fff<)<16  )


/* RGA_DST_INFO */
#define m_RGA2_DST_INFO_SW_DST_FMT                   (  0xf<<0 )
#define m_RGA2_DST_INFO_SW_DST_RB_SWAP               (  0x1<<4 )
#define m_RGA2_DST_INFO_SW_ALPHA_SWAP                (  0x1<<5 )
#define m_RGA2_DST_INFO_SW_DST_UV_SWAP               (  0x1<<6 )
#define m_RGA2_DST_INFO_SW_SRC1_FMT                  (  0x7<<7 )
#define m_RGA2_DST_INFO_SW_SRC1_RB_SWP               (  0x1<<10)
#define m_RGA2_DST_INFO_SW_SRC1_ALPHA_SWP            (  0x1<<11)
#define m_RGA2_DST_INFO_SW_DITHER_UP_E               (  0x1<<12)
#define m_RGA2_DST_INFO_SW_DITHER_DOWN_E             (  0x1<<13)
#define m_RGA2_DST_INFO_SW_DITHER_MODE               (  0x3<<14)
#define m_RGA2_DST_INFO_SW_DST_CSC_MODE              (  0x3<<16)    //add
#define m_RGA2_DST_INFO_SW_CSC_CLIP_MODE             (  0x1<<18)

#define s_RGA2_DST_INFO_SW_DST_FMT(x)                   (  (x&0xf)<<0 )
#define s_RGA2_DST_INFO_SW_DST_RB_SWAP(x)               (  (x&0x1)<<4 )
#define s_RGA2_DST_INFO_SW_ALPHA_SWAP(x)                (  (x&0x1)<<5 )
#define s_RGA2_DST_INFO_SW_DST_UV_SWAP(x)               (  (x&0x1)<<6 )
#define s_RGA2_DST_INFO_SW_SRC1_FMT(x)                  (  (x&0x7)<<7 )
#define s_RGA2_DST_INFO_SW_SRC1_RB_SWP(x)               (  (x&0x1)<<10)
#define s_RGA2_DST_INFO_SW_SRC1_ALPHA_SWP(x)            (  (x&0x1)<<11)
#define s_RGA2_DST_INFO_SW_DITHER_UP_E(x)               (  (x&0x1)<<12)
#define s_RGA2_DST_INFO_SW_DITHER_DOWN_E(x)             (  (x&0x1)<<13)
#define s_RGA2_DST_INFO_SW_DITHER_MODE(x)               (  (x&0x3)<<14)
#define s_RGA2_DST_INFO_SW_DST_CSC_MODE(x)              (  (x&0x3)<<16)    //add
#define s_RGA2_DST_INFO_SW_CSC_CLIP_MODE(x)             (  (x&0x1)<<18)


/* RGA_ALPHA_CTRL0 */
#define m_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_0             (  0x1<<0  )
#define m_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_SEL           (  0x1<<1  )
#define m_RGA2_ALPHA_CTRL0_SW_ROP_MODE                (  0x3<<2  )
#define m_RGA2_ALPHA_CTRL0_SW_SRC_GLOBAL_ALPHA        ( 0xff<<4  )
#define m_RGA2_ALPHA_CTRL0_SW_DST_GLOBAL_ALPHA        ( 0xff<<12 )
#define m_RGA2_ALPHA_CTRLO_SW_MASK_ENDIAN             (  0x1<<20 )         //add

#define s_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_0(x)             (  (x&0x1)<<0  )
#define s_RGA2_ALPHA_CTRL0_SW_ALPHA_ROP_SEL(x)           (  (x&0x1)<<1  )
#define s_RGA2_ALPHA_CTRL0_SW_ROP_MODE(x)                (  (x&0x3)<<2  )
#define s_RGA2_ALPHA_CTRL0_SW_SRC_GLOBAL_ALPHA(x)        ( (x&0xff)<<4  )
#define s_RGA2_ALPHA_CTRL0_SW_DST_GLOBAL_ALPHA(x)        ( (x&0xff)<<12 )
#define s_RGA2_ALPHA_CTRLO_SW_MASK_ENDIAN(x)             (  (x&0x1)<<20 )  //add



/* RGA_ALPHA_CTRL1 */
#define m_RGA2_ALPHA_CTRL1_SW_DST_COLOR_M0            ( 0x1<<0 )
#define m_RGA2_ALPHA_CTRL1_SW_SRC_COLOR_M0            ( 0x1<<1 )
#define m_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M0           ( 0x7<<2 )
#define m_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M0           ( 0x7<<5 )
#define m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M0        ( 0x1<<8 )
#define m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M0        ( 0x1<<9 )
#define m_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M0            ( 0x3<<10)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M0            ( 0x3<<12)
#define m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M0            ( 0x1<<14)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M0            ( 0x1<<15)
#define m_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M1           ( 0x7<<16)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M1           ( 0x7<<19)
#define m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M1        ( 0x1<<22)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M1        ( 0x1<<23)
#define m_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M1            ( 0x3<<24)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M1            ( 0x3<<26)
#define m_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M1            ( 0x1<<28)
#define m_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M1            ( 0x1<<29)

#define s_RGA2_ALPHA_CTRL1_SW_DST_COLOR_M0(x)            ( (x&0x1)<<0 )
#define s_RGA2_ALPHA_CTRL1_SW_SRC_COLOR_M0(x)            ( (x&0x1)<<1 )
#define s_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M0(x)           ( (x&0x7)<<2 )
#define s_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M0(x)           ( (x&0x7)<<5 )
#define s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M0(x)        ( (x&0x1)<<8 )
#define s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M0(x)        ( (x&0x1)<<9 )
#define s_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M0(x)            ( (x&0x3)<<10)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M0(x)            ( (x&0x3)<<12)
#define s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M0(x)            ( (x&0x1)<<14)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M0(x)            ( (x&0x1)<<15)
#define s_RGA2_ALPHA_CTRL1_SW_DST_FACTOR_M1(x)           ( (x&0x7)<<16)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_FACTOR_M1(x)           ( (x&0x7)<<19)
#define s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_CAL_M1(x)        ( (x&0x1)<<22)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_CAL_M1(x)        ( (x&0x1)<<23)
#define s_RGA2_ALPHA_CTRL1_SW_DST_BLEND_M1(x)            ( (x&0x3)<<24)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_BLEND_M1(x)            ( (x&0x3)<<26)
#define s_RGA2_ALPHA_CTRL1_SW_DST_ALPHA_M1(x)            ( (x&0x1)<<28)
#define s_RGA2_ALPHA_CTRL1_SW_SRC_ALPHA_M1(x)            ( (x&0x1)<<29)



/* RGA_MMU_CTRL1 */
#define m_RGA2_MMU_CTRL1_SW_SRC_MMU_EN                  (  0x1<<0 )
#define m_RGA2_MMU_CTRL1_SW_SRC_MMU_FLUSH               (  0x1<<1 )
#define m_RGA2_MMU_CTRL1_SW_SRC_MMU_PREFETCH_EN         (  0x1<<2 )
#define m_RGA2_MMU_CTRL1_SW_SRC_MMU_PREFETCH_DIR        (  0x1<<3 )
#define m_RGA2_MMU_CTRL1_SW_SRC1_MMU_EN                 (  0x1<<4 )
#define m_RGA2_MMU_CTRL1_SW_SRC1_MMU_FLUSH              (  0x1<<5 )
#define m_RGA2_MMU_CTRL1_SW_SRC1_MMU_PREFETCH_EN        (  0x1<<6 )
#define m_RGA2_MMU_CTRL1_SW_SRC1_MMU_PREFETCH_DIR       (  0x1<<7 )
#define m_RGA2_MMU_CTRL1_SW_DST_MMU_EN                  (  0x1<<8 )
#define m_RGA2_MMU_CTRL1_SW_DST_MMU_FLUSH               (  0x1<<9 )
#define m_RGA2_MMU_CTRL1_SW_DST_MMU_PREFETCH_EN         (  0x1<<10 )
#define m_RGA2_MMU_CTRL1_SW_DST_MMU_PREFETCH_DIR        (  0x1<<11 )
#define m_RGA2_MMU_CTRL1_SW_ELS_MMU_EN                  (  0x1<<12 )
#define m_RGA2_MMU_CTRL1_SW_ELS_MMU_FLUSH               (  0x1<<13 )

#define s_RGA2_MMU_CTRL1_SW_SRC_MMU_EN(x)                  (  (x&0x1)<<0 )
#define s_RGA2_MMU_CTRL1_SW_SRC_MMU_FLUSH(x)               (  (x&0x1)<<1 )
#define s_RGA2_MMU_CTRL1_SW_SRC_MMU_PREFETCH_EN(x)         (  (x&0x1)<<2 )
#define s_RGA2_MMU_CTRL1_SW_SRC_MMU_PREFETCH_DIR(x)        (  (x&0x1)<<3 )
#define s_RGA2_MMU_CTRL1_SW_SRC1_MMU_EN(x)                 (  (x&0x1)<<4 )
#define s_RGA2_MMU_CTRL1_SW_SRC1_MMU_FLUSH(x)              (  (x&0x1)<<5 )
#define s_RGA2_MMU_CTRL1_SW_SRC1_MMU_PREFETCH_EN(x)        (  (x&0x1)<<6 )
#define s_RGA2_MMU_CTRL1_SW_SRC1_MMU_PREFETCH_DIR(x)       (  (x&0x1)<<7 )
#define s_RGA2_MMU_CTRL1_SW_DST_MMU_EN(x)                  (  (x&0x1)<<8 )
#define s_RGA2_MMU_CTRL1_SW_DST_MMU_FLUSH(x)               (  (x&0x1)<<9 )
#define s_RGA2_MMU_CTRL1_SW_DST_MMU_PREFETCH_EN(x)         (  (x&0x1)<<10 )
#define s_RGA2_MMU_CTRL1_SW_DST_MMU_PREFETCH_DIR(x)        (  (x&0x1)<<11 )
#define s_RGA2_MMU_CTRL1_SW_ELS_MMU_EN(x)                  (  (x&0x1)<<12 )
#define s_RGA2_MMU_CTRL1_SW_ELS_MMU_FLUSH(x)               (  (x&0x1)<<13 )


#define RGA2_SYS_CTRL_OFFSET             0x0
#define RGA2_CMD_CTRL_OFFSET             0x4
#define RGA2_CMD_BASE_OFFSET             0x8
#define RGA2_STATUS_OFFSET               0xc
#define RGA2_INT_OFFSET                  0x10
#define RGA2_MMU_CTRL0_OFFSET            0x14
#define RGA2_MMU_CMD_BASE_OFFSET         0x18

#define RGA2_MODE_CTRL_OFFSET                   0x00
#define RGA2_SRC_INFO_OFFSET                    0x04
#define RGA2_SRC_BASE0_OFFSET                   0x08
#define RGA2_SRC_BASE1_OFFSET                   0x0c
#define RGA2_SRC_BASE2_OFFSET                   0x10
#define RGA2_SRC_BASE3_OFFSET                   0x14
#define RGA2_SRC_VIR_INFO_OFFSET                0x18
#define RGA2_SRC_ACT_INFO_OFFSET                0x1c
#define RGA2_SRC_X_FACTOR_OFFSET                0x20
#define RGA2_SRC_Y_FACTOR_OFFSET                0x24
#define RGA2_SRC_BG_COLOR_OFFSET                0x28
#define RGA2_SRC_FG_COLOR_OFFSET                0x2c
#define RGA2_SRC_TR_COLOR0_OFFSET               0x30
#define RGA2_CF_GR_A_OFFSET                     0x30 // repeat
#define RGA2_SRC_TR_COLOR1_OFFSET               0x34
#define RGA2_CF_GR_B_OFFSET                     0x34 // repeat
#define RGA2_DST_INFO_OFFSET                    0x38
#define RGA2_DST_BASE0_OFFSET                   0x3c
#define RGA2_DST_BASE1_OFFSET                   0x40
#define RGA2_DST_BASE2_OFFSET                   0x44
#define RGA2_DST_VIR_INFO_OFFSET                0x48
#define RGA2_DST_ACT_INFO_OFFSET                0x4c
#define RGA2_ALPHA_CTRL0_OFFSET                 0x50
#define RGA2_ALPHA_CTRL1_OFFSET                 0x54
#define RGA2_FADING_CTRL_OFFSET                 0x58
#define RGA2_PAT_CON_OFFSET                     0x5c
#define RGA2_ROP_CTRL0_OFFSET                   0x60
#define RGA2_CF_GR_G_OFFSET                     0x60 // repeat
#define RGA2_ROP_CTRL1_OFFSET                   0x64
#define RGA2_CF_GR_R_OFFSET                     0x64 // repeat
#define RGA2_MASK_BASE_OFFSET                   0x68
#define RGA2_MMU_CTRL1_OFFSET                   0x6c
#define RGA2_MMU_SRC_BASE_OFFSET                0x70
#define RGA2_MMU_SRC1_BASE_OFFSET               0x74
#define RGA2_MMU_DST_BASE_OFFSET                0x78
#define RGA2_MMU_ELS_BASE_OFFSET                0x7c

int RGA2_gen_reg_info(unsigned char *base, struct rga2_req *msg);
void RGA_MSG_2_RGA2_MSG(struct rga_req *req_rga, struct rga2_req *req);
void RGA_MSG_2_RGA2_MSG_32(struct rga_req_32 *req_rga, struct rga2_req *req);



#endif

