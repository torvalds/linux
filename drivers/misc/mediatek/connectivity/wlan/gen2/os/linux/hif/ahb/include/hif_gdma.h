/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/hif/sdio/include/hif.h#1
*/

/*! \file   "hif_gdma.h"
    \brief  MARCO, definition, structure for GDMA.

    MARCO, definition, structure for GDMA.
*/

/*
** Log: hif_gdma.h
 *
 * 01 16 2013 vend_samp.lin
 * Add AHB GDMA support
 * 1) Initial version
**
*/

#ifndef _HIF_GDMA_H
#define _HIF_GDMA_H

#include "mtk_porting.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef enum _MTK_WCN_HIF_GDMA_BURST_LEN {
	HIF_GDMA_BURST_1_8 = 0,
	HIF_GDMA_BURST_2_8,
	HIF_GDMA_BURST_3_8,
	HIF_GDMA_BURST_4_8,
	HIF_GDMA_BURST_5_8,
	HIF_GDMA_BURST_6_8,
	HIF_GDMA_BURST_7_8,
	HIF_GDMA_BURST_8_8	/* same as HIF_GDMA_BURST_7_8 */
} MTK_WCN_HIF_GDMA_BURST_LEN;

typedef enum _MTK_WCN_HIF_GDMA_WRITE_LEN {
	HIF_GDMA_WRITE_0 = 0,	/* transaction size is 1 byte */
	HIF_GDMA_WRITE_1,	/* transaction size is 2 byte */
	HIF_GDMA_WRITE_2,	/* transaction size is 4 byte */
	HIF_GDMA_WRITE_3	/* transaction size is 1 byte */
} MTK_WCN_HIF_GDMA_WRITE_LEN;

typedef enum _MTK_WCN_HIF_GDMA_RATIO {
	HIF_GDMA_RATIO_0 = 0,	/* 1/2 */
	HIF_GDMA_RATIO_1	/* 1/1 */
} MTK_WCN_HIF_GDMA_RATIO;

typedef enum _MTK_WCN_HIF_GDMA_CONNECT {
	HIF_GDMA_CONNECT_NO = 0,	/* no connect */
	HIF_GDMA_CONNECT_SET1,	/* connect set1 (req/ack) */
	HIF_GDMA_CONNECT_SET2,	/* connect set2 (req/ack) */
	HIF_GDMA_CONNECT_SET3	/* connect set3 (req/ack) */
} MTK_WCN_HIF_GDMA_CONNECT;

/* reference to MT6572_AP_P_DMA_Spec.doc */
#define AP_DMA_HIF_BASE             0x11000100

#define AP_P_DMA_G_DMA_2_INT_FLAG   (0x0000)
#define AP_P_DMA_G_DMA_2_CON        (0x0018)
#define AP_P_DMA_G_DMA_2_CONNECT    (0x0034)
#define AP_P_DMA_G_DMA_2_LEN1       (0x0024)
#define AP_P_DMA_G_DMA_2_SRC_ADDR   (0x001C)
#define AP_P_DMA_G_DMA_2_DST_ADDR   (0x0020)
#define AP_P_DMA_G_DMA_2_INT_EN     (0x0004)
#define AP_P_DMA_G_DMA_2_EN         (0x0008)
#define AP_P_DMA_G_DMA_2_RST        (0x000C)
#define AP_P_DMA_G_DMA_2_STOP       (0x0010)

#define AP_DMA_HIF_0_LENGTH         0x0038

/* AP_DMA_HIF_0_INT_FLAG */
#define ADH_CR_FLAG_0               BIT(0)

/* AP_DMA_HIF_0_INT_EN */
#define ADH_CR_INTEN_FLAG_0         BIT(0)

/* AP_DMA_HIF_0_EN */
#define ADH_CR_EN                   BIT(0)
#define ADH_CR_CONN_BUR_EN          BIT(1)

/* AP_DMA_HIF_0_STOP */
#define ADH_CR_PAUSE                BIT(1)
#define ADH_CR_STOP                 BIT(0)

/* AP_P_DMA_G_DMA_2_CON */
#define ADH_CR_FLAG_FINISH          BIT(30)
#define ADH_CR_RSIZE                BITS(28, 29)
#define ADH_CR_RSIZE_OFFSET         28
#define ADH_CR_WSIZE                BITS(24, 25)
#define ADH_CR_WSIZE_OFFSET         24
#define ADH_CR_BURST_LEN            BITS(16, 18)
#define ADH_CR_BURST_LEN_OFFSET     16
#define ADH_CR_WADDR_FIX_EN         BIT(3)
#define ADH_CR_WADDR_FIX_EN_OFFSET  3
#define ADH_CR_RADDR_FIX_EN         BIT(4)
#define ADH_CR_RADDR_FIX_EN_OFFSET  4

/* AP_P_DMA_G_DMA_2_CONNECT */
#define ADH_CR_RATIO                BIT(3)
#define ADH_CR_RATIO_OFFSET         3
#define ADH_CR_DIR                  BIT(2)
#define ADH_CR_DIR_OFFSET           2
#define ADH_CR_CONNECT              BITS(0, 1)

/* AP_DMA_HIF_0_LEN */
#define ADH_CR_LEN                  BITS(0, 19)

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _HIF_GDMA_H */

/* End of hif_gdma.h */
