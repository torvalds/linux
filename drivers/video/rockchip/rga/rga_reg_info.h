#ifndef __REG_INFO_H__
#define __REG_INFO_H__


//#include "chip_register.h"

//#include "rga_struct.h"
#include "rga.h"

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

//RGA register map

//General Registers
#define rRGA_SYS_CTRL             (*(volatile uint32_t *)(RGA_BASE + RGA_SYS_CTRL))
#define rRGA_CMD_CTRL             (*(volatile uint32_t *)(RGA_BASE + RGA_CMD_CTRL))
#define rRGA_CMD_ADDR             (*(volatile uint32_t *)(RGA_BASE + RGA_CMD_ADDR))
#define rRGA_STATUS               (*(volatile uint32_t *)(RGA_BASE + RGA_STATUS))
#define rRGA_INT                  (*(volatile uint32_t *)(RGA_BASE + RGA_INT))
#define rRGA_AXI_ID               (*(volatile uint32_t *)(RGA_BASE + RGA_AXI_ID))
#define rRGA_MMU_STA_CTRL         (*(volatile uint32_t *)(RGA_BASE + RGA_MMU_STA_CTRL))
#define rRGA_MMU_STA              (*(volatile uint32_t *)(RGA_BASE + RGA_MMU_STA))

//Command code start
#define rRGA_MODE_CTRL            (*(volatile uint32_t *)(RGA_BASE + RGA_MODE_CTRL))

//Source Image Registers
#define rRGA_SRC_Y_MST            (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_Y_MST))
#define rRGA_SRC_CB_MST           (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_CB_MST))
#define rRGA_MASK_READ_MST        (*(volatile uint32_t *)(RGA_BASE + RGA_MASK_READ_MST))  //repeat
#define rRGA_SRC_CR_MST           (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_CR_MST))
#define rRGA_SRC_VIR_INFO         (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_VIR_INFO))
#define rRGA_SRC_ACT_INFO         (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_ACT_INFO))
#define rRGA_SRC_X_PARA           (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_X_PARA))
#define rRGA_SRC_Y_PARA           (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_Y_PARA))
#define rRGA_SRC_TILE_XINFO       (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_XINFO))
#define rRGA_SRC_TILE_YINFO       (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_YINFO))
#define rRGA_SRC_TILE_H_INCR      (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_H_INCR))
#define rRGA_SRC_TILE_V_INCR      (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_V_INCR))
#define rRGA_SRC_TILE_OFFSETX     (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_OFFSETX))
#define rRGA_SRC_TILE_OFFSETY     (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TILE_OFFSETY))
#define rRGA_SRC_BG_COLOR         (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_BG_COLOR))
#define rRGA_SRC_FG_COLOR         (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_FG_COLOR))
#define rRGA_LINE_DRAWING_COLOR   (*(volatile uint32_t *)(RGA_BASE + RGA_LINE_DRAWING_COLOR))  //repeat
#define rRGA_SRC_TR_COLOR0        (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TR_COLOR0))
#define rRGA_CP_GR_A              (*(volatile uint32_t *)(RGA_BASE + RGA_CP_GR_A))  //repeat
#define rRGA_SRC_TR_COLOR1        (*(volatile uint32_t *)(RGA_BASE + RGA_SRC_TR_COLOR1))
#define rRGA_CP_GR_B              (*(volatile uint32_t *)(RGA_BASE + RGA_CP_GR_B))  //repeat

#define rRGA_LINE_DRAW            (*(volatile uint32_t *)(RGA_BASE + RGA_LINE_DRAW))
#define rRGA_PAT_START_POINT      (*(volatile uint32_t *)(RGA_BASE + RGA_PAT_START_POINT))  //repeat

//Destination Image Registers
#define rRGA_DST_MST              (*(volatile uint32_t *)(RGA_BASE + RGA_DST_MST))
#define rRGA_LUT_MST              (*(volatile uint32_t *)(RGA_BASE + RGA_LUT_MST))  //repeat
#define rRGA_PAT_MST              (*(volatile uint32_t *)(RGA_BASE + RGA_PAT_MST))  //repeat
#define rRGA_LINE_DRAWING_MST     (*(volatile uint32_t *)(RGA_BASE + RGA_LINE_DRAWING_MST))  //repeat

#define rRGA_DST_VIR_INFO         (*(volatile uint32_t *)(RGA_BASE + RGA_DST_VIR_INFO))

#define rRGA_DST_CTR_INFO         (*(volatile uint32_t *)(RGA_BASE + RGA_DST_CTR_INFO))
#define rRGA_LINE_DRAW_XY_INFO    (*(volatile uint32_t *)(RGA_BASE + RGA_LINE_DRAW_XY_INFO))  //repeat 

//Alpha/ROP Registers
#define rRGA_ALPHA_CON            (*(volatile uint32_t *)(RGA_BASE + RGA_ALPHA_CON))
#define rRGA_FADING_CON           (*(volatile uint32_t *)(RGA_BASE + RGA_FADING_CON))

#define rRGA_PAT_CON              (*(volatile uint32_t *)(RGA_BASE + RGA_PAT_CON))
#define rRGA_DST_VIR_WIDTH_PIX    (*(volatile uint32_t *)(RGA_BASE + RGA_DST_VIR_WIDTH_PIX))  //repeat

#define rRGA_ROP_CON0             (*(volatile uint32_t *)(RGA_BASE + RGA_ROP_CON0))
#define rRGA_CP_GR_G              (*(volatile uint32_t *)(RGA_BASE + RGA_CP_GR_G))  //repeat
#define rRGA_PRESCL_CB_MST        (*(volatile uint32_t *)(RGA_BASE + RGA_PRESCL_CB_MST))  //repeat

#define rRGA_ROP_CON1             (*(volatile uint32_t *)(RGA_BASE + RGA_ROP_CON1))
#define rRGA_CP_GR_R              (*(volatile uint32_t *)(RGA_BASE + RGA_CP_GR_R))  //repeat
#define rRGA_PRESCL_CR_MST        (*(volatile uint32_t *)(RGA_BASE + RGA_PRESCL_CR_MST))  //repeat

//MMU Register
#define rRGA_MMU_CTRL             (*(volatile uint32_t *)(RGA_BASE + RGA_MMU_CTRL))




//-----------------------------------------------------------------
//reg detail definition
//-----------------------------------------------------------------
/*RGA_SYS_CTRL*/
#define m_RGA_SYS_CTRL_CMD_MODE                   ( 1<<2 )
#define m_RGA_SYS_CTRL_OP_ST_SLV                  ( 1<<1 )
#define m_RGA_sys_CTRL_SOFT_RESET                 ( 1<<0 )

#define s_RGA_SYS_CTRL_CMD_MODE(x)                ( (x&0x1)<<2 )
#define s_RGA_SYS_CTRL_OP_ST_SLV(x)               ( (x&0x1)<<1 )
#define s_RGA_sys_CTRL_SOFT_RESET(x)              ( (x&0x1)<<0 )


/*RGA_CMD_CTRL*/
#define m_RGA_CMD_CTRL_CMD_INCR_NUM               ( 0x3ff<<3 )
#define m_RGA_CMD_CTRL_CMD_STOP_MODE              (     1<<2 )
#define m_RGA_CMD_CTRL_CMD_INCR_VALID             (     1<<1 )
#define m_RGA_CMD_CTRL_CMD_LINE_FET_ST            (     1<<0 )

#define s_RGA_CMD_CTRL_CMD_INCR_NUM(x)            ( (x&0x3ff)<<3 )
#define s_RGA_CMD_CTRL_CMD_STOP_MODE(x)           (   (x&0x1)<<2 )
#define s_RGA_CMD_CTRL_CMD_INCR_VALID(x)          (   (x&0x1)<<1 )
#define s_RGA_CMD_CTRL_CMD_LINE_FET_ST(x)         (   (x*0x1)<<0 )


/*RGA_STATUS*/
#define m_RGA_CMD_STATUS_CMD_TOTAL_NUM            ( 0xfff<<20 )
#define m_RGA_CMD_STATUS_NOW_CMD_NUM              ( 0xfff<<8  )
#define m_RGA_CMD_STATUS_ENGINE_STATUS            (     1<<0  )


/*RGA_INT*/
#define m_RGA_INT_ALL_CMD_DONE_INT_EN             ( 1<<10 )
#define m_RGA_INT_MMU_INT_EN                      ( 1<<9  )
#define m_RGA_INT_ERROR_INT_EN                    ( 1<<8  )
#define m_RGA_INT_NOW_CMD_DONE_INT_CLEAR          ( 1<<7  )
#define m_RGA_INT_ALL_CMD_DONE_INT_CLEAR          ( 1<<6  )
#define m_RGA_INT_MMU_INT_CLEAR                   ( 1<<5  )
#define m_RGA_INT_ERROR_INT_CLEAR                 ( 1<<4  )
#define m_RGA_INT_NOW_CMD_DONE_INT_FLAG           ( 1<<3  )
#define m_RGA_INT_ALL_CMD_DONE_INT_FLAG           ( 1<<2  )
#define m_RGA_INT_MMU_INT_FLAG                    ( 1<<1  )
#define m_RGA_INT_ERROR_INT_FLAG                  ( 1<<0  )

#define s_RGA_INT_ALL_CMD_DONE_INT_EN(x)          ( (x&0x1)<<10 )
#define s_RGA_INT_MMU_INT_EN(x)                   ( (x&0x1)<<9  )
#define s_RGA_INT_ERROR_INT_EN(x)                 ( (x&0x1)<<8  )
#define s_RGA_INT_NOW_CMD_DONE_INT_CLEAR(x)       ( (x&0x1)<<7  )
#define s_RGA_INT_ALL_CMD_DONE_INT_CLEAR(x)       ( (x&0x1)<<6  )
#define s_RGA_INT_MMU_INT_CLEAR(x)                ( (x&0x1)<<5  )
#define s_RGA_INT_ERROR_INT_CLEAR(x)              ( (x&0x1)<<4  )


/*RGA_AXI_ID*/
#define m_RGA_AXI_ID_MMU_READ                     ( 3<<30 )
#define m_RGA_AXI_ID_MMU_WRITE                    ( 3<<28 )
#define m_RGA_AXI_ID_MASK_READ                    ( 0xf<<24 )
#define m_RGA_AXI_ID_CMD_FET                      ( 0xf<<20 )
#define m_RGA_AXI_ID_DST_WRITE                    ( 0xf<<16 )
#define m_RGA_AXI_ID_DST_READ                     ( 0xf<<12 )
#define m_RGA_AXI_ID_SRC_CR_READ                  ( 0xf<<8  )
#define m_RGA_AXI_ID_SRC_CB_READ                  ( 0xf<<4  )
#define m_RGA_AXI_ID_SRC_Y_READ                   ( 0xf<<0  )

#define s_RGA_AXI_ID_MMU_READ(x)                  ( (x&0x3)<<30 )
#define s_RGA_AXI_ID_MMU_WRITE(x)                 ( (x&0x3)<<28 )
#define s_RGA_AXI_ID_MASK_READ(x)                 ( (x&0xf)<<24 )
#define s_RGA_AXI_ID_CMD_FET(x)                   ( (x&0xf)<<20 )
#define s_RGA_AXI_ID_DST_WRITE(x)                 ( (x&0xf)<<16 )
#define s_RGA_AXI_ID_DST_READ(x)                  ( (x&0xf)<<12 )
#define s_RGA_AXI_ID_SRC_CR_READ(x)               ( (x&0xf)<<8  )
#define s_RGA_AXI_ID_SRC_CB_READ(x)               ( (x&0xf)<<4  )
#define s_RGA_AXI_ID_SRC_Y_READ(x)                ( (x&0xf)<<0  )


/*RGA_MMU_STA_CTRL*/
#define m_RGA_MMU_STA_CTRL_TLB_STA_CLEAR          ( 1<<3 )
#define m_RGA_MMU_STA_CTRL_TLB_STA_RESUME         ( 1<<2 )
#define m_RGA_MMU_STA_CTRL_TLB_STA_PAUSE          ( 1<<1 )
#define m_RGA_MMU_STA_CTRL_TLB_STA_EN             ( 1<<0 )

#define s_RGA_MMU_STA_CTRL_TLB_STA_CLEAR(x)       ( (x&0x1)<<3 )
#define s_RGA_MMU_STA_CTRL_TLB_STA_RESUME(x)      ( (x&0x1)<<2 )
#define s_RGA_MMU_STA_CTRL_TLB_STA_PAUSE(x)       ( (x&0x1)<<1 )
#define s_RGA_MMU_STA_CTRL_TLB_STA_EN(x)          ( (x&0x1)<<0 )



/* RGA_MODE_CTRL */
#define m_RGA_MODE_CTRL_2D_RENDER_MODE            (  7<<0  )
#define m_RGA_MODE_CTRL_SRC_RGB_PACK              (  1<<3  )
#define m_RGA_MODE_CTRL_SRC_FORMAT                ( 15<<4  )
#define m_RGA_MODE_CTRL_SRC_RB_SWAP               (  1<<8  )
#define m_RGA_MODE_CTRL_SRC_ALPHA_SWAP            (  1<<9  )
#define m_RGA_MODE_CTRL_SRC_UV_SWAP_MODE          (  1<<10 )
#define m_RGA_MODE_CTRL_YUV2RGB_CON_MODE          (  3<<11 )
#define m_RGA_MODE_CTRL_SRC_TRANS_MODE           (0x1f<<13 )
#define m_RGA_MODE_CTRL_SRC_TR_MODE               (  1<<13 )
#define m_RGA_MODE_CTRL_SRC_TR_R_EN               (  1<<14 )
#define m_RGA_MODE_CTRL_SRC_TR_G_EN               (  1<<15 )
#define m_RGA_MODE_CTRL_SRC_TR_B_EN               (  1<<16 )
#define m_RGA_MODE_CTRL_SRC_TR_A_EN               (  1<<17 )
#define m_RGA_MODE_CTRL_ROTATE_MODE               (  3<<18 )
#define m_RGA_MODE_CTRL_SCALE_MODE                (  3<<20 )
#define m_RGA_MODE_CTRL_PAT_SEL                   (  1<<22 )
#define m_RGA_MODE_CTRL_DST_FORMAT                (  3<<23 )
#define m_RGA_MODE_CTRL_DST_RGB_PACK              (  1<<25 )
#define m_RGA_MODE_CTRL_DST_RB_SWAP               (  1<<26 )
#define m_RGA_MODE_CTRL_DST_ALPHA_SWAP            (  1<<27 )
#define m_RGA_MODE_CTRL_LUT_ENDIAN_MODE           (  1<<28 )
#define m_RGA_MODE_CTRL_CMD_INT_ENABLE            (  1<<29 )
#define m_RGA_MODE_CTRL_ZERO_MODE_ENABLE          (  1<<30 )
#define m_RGA_MODE_CTRL_DST_ALPHA_ENABLE          (  1<<30 )



#define s_RGA_MODE_CTRL_2D_RENDER_MODE(x)         (  (x&0x7)<<0  )
#define s_RGA_MODE_CTRL_SRC_RGB_PACK(x)           (  (x&0x1)<<3  )
#define s_RGA_MODE_CTRL_SRC_FORMAT(x)             (  (x&0xf)<<4  )
#define s_RGA_MODE_CTRL_SRC_RB_SWAP(x)            (  (x&0x1)<<8  )
#define s_RGA_MODE_CTRL_SRC_ALPHA_SWAP(x)         (  (x&0x1)<<9  )
#define s_RGA_MODE_CTRL_SRC_UV_SWAP_MODE(x)       (  (x&0x1)<<10 )
#define s_RGA_MODE_CTRL_YUV2RGB_CON_MODE(x)       (  (x&0x3)<<11 )
#define s_RGA_MODE_CTRL_SRC_TRANS_MODE(x)         ( (x&0x1f)<<13 )
#define s_RGA_MODE_CTRL_SRC_TR_MODE(x)            (  (x&0x1)<<13 )
#define s_RGA_MODE_CTRL_SRC_TR_R_EN(x)            (  (x&0x1)<<14 )
#define s_RGA_MODE_CTRL_SRC_TR_G_EN(x)            (  (x&0x1)<<15 )
#define s_RGA_MODE_CTRL_SRC_TR_B_EN(x)            (  (x&0x1)<<16 )
#define s_RGA_MODE_CTRL_SRC_TR_A_EN(x)            (  (x&0x1)<<17 )
#define s_RGA_MODE_CTRL_ROTATE_MODE(x)            (  (x&0x3)<<18 )
#define s_RGA_MODE_CTRL_SCALE_MODE(x)             (  (x&0x3)<<20 )
#define s_RGA_MODE_CTRL_PAT_SEL(x)                (  (x&0x1)<<22 )
#define s_RGA_MODE_CTRL_DST_FORMAT(x)             (  (x&0x3)<<23 )
#define s_RGA_MODE_CTRL_DST_RGB_PACK(x)           (  (x&0x1)<<25 )
#define s_RGA_MODE_CTRL_DST_RB_SWAP(x)            (  (x&0x1)<<26 )
#define s_RGA_MODE_CTRL_DST_ALPHA_SWAP(x)         (  (x&0x1)<<27 )
#define s_RGA_MODE_CTRL_LUT_ENDIAN_MODE(x)        (  (x&0x1)<<28 )
#define s_RGA_MODE_CTRL_CMD_INT_ENABLE(x)         (  (x&0x1)<<29 )
#define s_RGA_MODE_CTRL_ZERO_MODE_ENABLE(x)       (  (x&0x1)<<30 )
#define s_RGA_MODE_CTRL_DST_ALPHA_ENABLE(x)       (  (x&0x1)<<31 )



/* RGA_LINE_DRAW */
#define m_RGA_LINE_DRAW_MAJOR_WIDTH            ( 0x7ff<<0 )
#define m_RGA_LINE_DRAW_LINE_DIRECTION         (   0x1<<11)
#define m_RGA_LINE_DRAW_LINE_WIDTH             (   0xf<<12)
#define m_RGA_LINE_DRAW_INCR_VALUE             ( 0xfff<<16)
#define m_RGA_LINE_DRAW_DIR_MAJOR              (   0x1<<28)
#define m_RGA_LINE_DRAW_DIR_SEMI_MAJOR         (   0x1<<29)
#define m_RGA_LINE_DRAW_LAST_POINT             (   0x1<<30)
#define m_RGA_LINE_DRAW_ANTI_ALISING           (   0x1<<31)

#define s_RGA_LINE_DRAW_MAJOR_WIDTH(x)            (((x)&0x7ff)<<0 )
#define s_RGA_LINE_DRAW_LINE_DIRECTION(x)         (  ((x)&0x1)<<11)
#define s_RGA_LINE_DRAW_LINE_WIDTH(x)             (  ((x)&0xf)<<12)
#define s_RGA_LINE_DRAW_INCR_VALUE(x)             (((x)&0xfff)<<16)
#define s_RGA_LINE_DRAW_DIR_MAJOR(x)              (  ((x)&0x1)<<28)
#define s_RGA_LINE_DRAW_DIR_SEMI_MAJOR(x)         (  ((x)&0x1)<<29)
#define s_RGA_LINE_DRAW_LAST_POINT(x)             (  ((x)&0x1)<<30)
#define s_RGA_LINE_DRAW_ANTI_ALISING(x)           (  ((x)&0x1)<<31)

 
/* RGA_ALPHA_CON */
#define m_RGA_ALPHA_CON_ENABLE                  ( 0x1<<0 )
#define m_RGA_ALPHA_CON_A_OR_R_SEL              ( 0x1<<1 )
#define m_RGA_ALPHA_CON_ALPHA_MODE              ( 0x3<<2 )
#define m_RGA_ALPHA_CON_PD_MODE                 ( 0xf<<4 )
#define m_RGA_ALPHA_CON_SET_CONSTANT_VALUE      (0xff<<8 )
#define m_RGA_ALPHA_CON_PD_M_SEL                ( 0x1<<16)
#define m_RGA_ALPHA_CON_FADING_ENABLE           ( 0x1<<17)
#define m_RGA_ALPHA_CON_ROP_MODE_SEL            ( 0x3<<18)
#define m_RGA_ALPHA_CON_CAL_MODE_SEL            ( 0x1<<28)
#define m_RGA_ALPHA_CON_DITHER_ENABLE           ( 0x1<<29)
#define m_RGA_ALPHA_CON_GRADIENT_CAL_MODE       ( 0x1<<30)
#define m_RGA_ALPHA_CON_AA_SEL                  ( 0x1<<31)

#define s_RGA_ALPHA_CON_ENABLE(x)                  ( (x&0x1)<<0 )
#define s_RGA_ALPHA_CON_A_OR_R_SEL(x)              ( (x&0x1)<<1 )
#define s_RGA_ALPHA_CON_ALPHA_MODE(x)              ( (x&0x3)<<2 )
#define s_RGA_ALPHA_CON_PD_MODE(x)                 ( (x&0xf)<<4 )
#define s_RGA_ALPHA_CON_SET_CONSTANT_VALUE(x)      ((x&0xff)<<8 )
#define s_RGA_ALPHA_CON_PD_M_SEL(x)                ( (x&0x1)<<16)
#define s_RGA_ALPHA_CON_FADING_ENABLE(x)           ( (x&0x1)<<17)
#define s_RGA_ALPHA_CON_ROP_MODE_SEL(x)            ( (x&0x3)<<18)
#define s_RGA_ALPHA_CON_CAL_MODE_SEL(x)            ( (x&0x1)<<28)
#define s_RGA_ALPHA_CON_DITHER_ENABLE(x)           ( (x&0x1)<<29)
#define s_RGA_ALPHA_CON_GRADIENT_CAL_MODE(x)       ( (x&0x1)<<30)
#define s_RGA_ALPHA_CON_AA_SEL(x)                  ( (x&0x1)<<31)


/* blur sharp mode */
#define m_RGA_BLUR_SHARP_FILTER_MODE                    (  0x1<<25 )
#define m_RGA_BLUR_SHARP_FILTER_TYPE                    (  0x3<<26 )

#define s_RGA_BLUR_SHARP_FILTER_MODE(x)                 ( (x&0x1)<<25 )
#define s_RGA_BLUR_SHARP_FILTER_TYPE(x)                 ( (x&0x3)<<26 )


/* pre scale mode */
#define m_RGA_PRE_SCALE_HOR_RATIO                       ( 0x3 <<20 )
#define m_RGA_PRE_SCALE_VER_RATIO                       ( 0x3 <<22 )
#define m_RGA_PRE_SCALE_OUTPUT_FORMAT                   ( 0x1 <<24 )

#define s_RGA_PRE_SCALE_HOR_RATIO(x)                    ( (x&0x3) <<20 )
#define s_RGA_PRE_SCALE_VER_RATIO(x)                    ( (x&0x3) <<22 )
#define s_RGA_PRE_SCALE_OUTPUT_FORMAT(x)                ( (x&0x1) <<24 )



/* RGA_MMU_CTRL*/
#define m_RGA_MMU_CTRL_TLB_ADDR                         ( 0xffffffff<<0)
#define m_RGA_MMU_CTRL_PAGE_TABLE_SIZE                  ( 0x3<<4 )
#define m_RGA_MMU_CTRL_MMU_ENABLE                       ( 0x1<<0 )
#define m_RGA_MMU_CTRL_SRC_FLUSH                        ( 0x1<<1 )
#define m_RGA_MMU_CTRL_DST_FLUSH                        ( 0x1<<2 )
#define m_RGA_MMU_CTRL_CMD_CHAN_FLUSH                   ( 0x1<<3 )

#define s_RGA_MMU_CTRL_TLB_ADDR(x)                      ((x&0xffffffff))
#define s_RGA_MMU_CTRL_PAGE_TABLE_SIZE(x)               ((x&0x3)<<4)
#define s_RGA_MMU_CTRL_MMU_ENABLE(x)                    ((x&0x1)<<0)
#define s_RGA_MMU_CTRL_SRC_FLUSH(x)                     ((x&0x1)<<1)
#define s_RGA_MMU_CTRL_DST_FLUSH(x)                     ((x&0x1)<<2)
#define s_RGA_MMU_CTRL_CMD_CHAN_FLUSH(x)                ((x&0x1)<<3)

#endif

/*
#define RGA_MODE_CTRL_OFFSET            0x0
#define RGA_SRC_Y_MST_OFFSET            0x4
#define RGA_SRC_CB_MST_OFFSET           0x8
#define RGA_SRC_CR_MST_OFFSET           0xc
#define RGA_SRC_VIR_INFO_OFFSET         0x10
#define RGA_SRC_ACT_INFO_OFFSET         0x14
#define RGA_SRC_X_PARA_OFFSET           0x18
#define RGA_SRC_Y_PARA_OFFSET           0x1c
#define RGA_SRC_TILE_XINFO_OFFSET       0x20
#define RGA_SRC_TILE_YINFO_OFFSET       0x24
#define RGA_SRC_TILE_H_INCR_OFFSET      0x28
#define RGA_SRC_TILE_V_INCR_OFFSET      0x2c
#define RGA_SRC_TILE_OFFSETX_OFFSET     0x30
#define RGA_SRC_TILE_OFFSETY_OFFSET     0x34
#define RGA_SRC_BG_COLOR_OFFSET         0x38

#define RGA_SRC_FG_COLOR_OFFSET         0x3c
#define RGA_LINE_DRAWING_COLOR_OFFSET   0x3c

#define RGA_SRC_TR_COLOR0_OFFSET        0x40
#define RGA_CP_GR_A_OFFSET              0x40  //repeat

#define RGA_SRC_TR_COLOR1_OFFSET        0x44
#define RGA_CP_GR_B_OFFSET              0x44  //repeat

#define RGA_LINE_DRAW_OFFSET            0x48
#define RGA_PAT_START_POINT_OFFSET      0x48  //repeat

#define RGA_DST_MST_OFFSET              0x4c
#define RGA_LUT_MST_OFFSET              0x4c  //repeat
#define RGA_PAT_MST_OFFSET              0x4c  //repeat
#define RGA_LINE_DRAWING_MST_OFFSET     0x4c  //repeat

#define RGA_DST_VIR_INFO_OFFSET         0x50

#define RGA_DST_CTR_INFO_OFFSET         0x54
#define RGA_LINE_DRAW_XY_INFO_OFFSET    0x54  //repeat 

#define RGA_ALPHA_CON_OFFSET            0x58
#define RGA_FADING_CON_OFFSET           0x5c

#define RGA_PAT_CON_OFFSET              0x60
#define RGA_LINE_DRAWING_WIDTH_OFFSET   0x60  //repeat

#define RGA_ROP_CON0_OFFSET             0x64
#define RGA_CP_GR_G_OFFSET              0x64  //repeat
#define RGA_PRESCL_CB_MST_OFFSET        0x64  //repeat

#define RGA_ROP_CON1_OFFSET             0x68
#define RGA_CP_GR_R_OFFSET              0x68  //repeat
#define RGA_PRESCL_CR_MST_OFFSET        0x68  //repeat

#define RGA_MMU_CTRL_OFFSET             0x6c


#define RGA_SYS_CTRL_OFFSET             0x000
#define RGA_CMD_CTRL_OFFSET             0x004
#define RGA_CMD_ADDR_OFFSET             0x008
#define RGA_STATUS_OFFSET               0x00c
#define RGA_INT_OFFSET                  0x010
#define RGA_AXI_ID_OFFSET               0x014
#define RGA_MMU_STA_CTRL_OFFSET         0x018
#define RGA_MMU_STA_OFFSET              0x01c
*/
//hxx

#define RGA_SYS_CTRL_OFFSET             (RGA_SYS_CTRL-0x100)
#define RGA_CMD_CTRL_OFFSET             (RGA_CMD_CTRL-0x100)
#define RGA_CMD_ADDR_OFFSET             (RGA_CMD_ADDR-0x100)
#define RGA_STATUS_OFFSET               (RGA_STATUS-0x100)
#define RGA_INT_OFFSET                  (RGA_INT-0x100)
#define RGA_AXI_ID_OFFSET               (RGA_AXI_ID-0x100)
#define RGA_MMU_STA_CTRL_OFFSET         (RGA_MMU_STA_CTRL-0x100)
#define RGA_MMU_STA_OFFSET              (RGA_MMU_STA-0x100)

#define RGA_MODE_CTRL_OFFSET            (RGA_MODE_CTRL-0x100)
#define RGA_SRC_Y_MST_OFFSET            (RGA_SRC_Y_MST-0x100)
#define RGA_SRC_CB_MST_OFFSET           (RGA_SRC_CB_MST-0x100)
#define RGA_SRC_CR_MST_OFFSET           (RGA_SRC_CR_MST-0x100)
#define RGA_SRC_VIR_INFO_OFFSET         (RGA_SRC_VIR_INFO-0x100)
#define RGA_SRC_ACT_INFO_OFFSET         (RGA_SRC_ACT_INFO-0x100)
#define RGA_SRC_X_PARA_OFFSET           (RGA_SRC_X_PARA-0x100)
#define RGA_SRC_Y_PARA_OFFSET           (RGA_SRC_Y_PARA-0x100)
#define RGA_SRC_TILE_XINFO_OFFSET       (RGA_SRC_TILE_XINFO-0x100)
#define RGA_SRC_TILE_YINFO_OFFSET       (RGA_SRC_TILE_YINFO-0x100)
#define RGA_SRC_TILE_H_INCR_OFFSET      (RGA_SRC_TILE_H_INCR-0x100)
#define RGA_SRC_TILE_V_INCR_OFFSET      (RGA_SRC_TILE_V_INCR-0x100)
#define RGA_SRC_TILE_OFFSETX_OFFSET     (RGA_SRC_TILE_OFFSETX-0x100)
#define RGA_SRC_TILE_OFFSETY_OFFSET     (RGA_SRC_TILE_OFFSETY-0x100)
#define RGA_SRC_BG_COLOR_OFFSET         (RGA_SRC_BG_COLOR-0x100)

#define RGA_SRC_FG_COLOR_OFFSET         (RGA_SRC_FG_COLOR-0x100)
#define RGA_LINE_DRAWING_COLOR_OFFSET   (RGA_LINE_DRAWING_COLOR-0x100)

#define RGA_SRC_TR_COLOR0_OFFSET        (RGA_SRC_TR_COLOR0-0x100)
#define RGA_CP_GR_A_OFFSET              (RGA_CP_GR_A-0x100)  //repeat

#define RGA_SRC_TR_COLOR1_OFFSET        (RGA_SRC_TR_COLOR1-0x100)
#define RGA_CP_GR_B_OFFSET              (RGA_CP_GR_B-0x100)  //repeat

#define RGA_LINE_DRAW_OFFSET            (RGA_LINE_DRAW-0x100)
#define RGA_PAT_START_POINT_OFFSET      (RGA_PAT_START_POINT-0x100)  //repeat

#define RGA_DST_MST_OFFSET              (RGA_DST_MST-0x100)
#define RGA_LUT_MST_OFFSET              (RGA_LUT_MST-0x100)  //repeat
#define RGA_PAT_MST_OFFSET              (RGA_PAT_MST-0x100)  //repeat
#define RGA_LINE_DRAWING_MST_OFFSET     (RGA_LINE_DRAWING_MST-0x100)  //repeat

#define RGA_DST_VIR_INFO_OFFSET         (RGA_DST_VIR_INFO-0x100)

#define RGA_DST_CTR_INFO_OFFSET         (RGA_DST_CTR_INFO-0x100)
#define RGA_LINE_DRAW_XY_INFO_OFFSET    (RGA_LINE_DRAW_XY_INFO-0x100)  //repeat 

#define RGA_ALPHA_CON_OFFSET            (RGA_ALPHA_CON-0x100)

#define RGA_PAT_CON_OFFSET              (RGA_PAT_CON-0x100)
#define RGA_LINE_DRAWING_WIDTH_OFFSET   (RGA_DST_VIR_WIDTH_PIX-0x100)  //repeat

#define RGA_ROP_CON0_OFFSET             (RGA_ROP_CON0-0x100)
#define RGA_CP_GR_G_OFFSET              (RGA_CP_GR_G-0x100)  //repeat
#define RGA_PRESCL_CB_MST_OFFSET        (RGA_PRESCL_CB_MST-0x100)  //repeat

#define RGA_ROP_CON1_OFFSET             (RGA_ROP_CON1-0x100)
#define RGA_CP_GR_R_OFFSET              (RGA_CP_GR_R-0x100)  //repeat
#define RGA_PRESCL_CR_MST_OFFSET        (RGA_PRESCL_CR_MST-0x100)  //repeat

#define RGA_FADING_CON_OFFSET           (RGA_FADING_CON-0x100)
#define RGA_MMU_TLB_OFFSET             (RGA_MMU_TBL-0x100)


void matrix_cal(const struct rga_req *msg, TILE_INFO *tile);


unsigned int RGA_gen_reg_info(const struct rga_req *msg, unsigned char *base);
uint8_t   RGA_pixel_width_init(uint32_t format);

