/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/hif/sdio/include/hif.h#1
*/

/*! \file   "hif_pdma.h"
    \brief  MARCO, definition, structure for PDMA.

    MARCO, definition, structure for PDMA.
*/

/*
** Log: hif_pdma.h
 *
 * 01 16 2013 vend_samp.lin
 * Add AHB PDMA support
 * 1) Initial version
**
*/

#ifndef _HIF_PDMA_H
#define _HIF_PDMA_H

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

typedef enum _MTK_WCN_HIF_PDMA_BURST_LEN {
	HIF_PDMA_BURST_1_4 = 0,
	HIF_PDMA_BURST_2_4,
	HIF_PDMA_BURST_3_4,
	HIF_PDMA_BURST_4_4
} MTK_WCN_HIF_PDMA_BURST_LEN;

/* reference to MT6572_AP_P_DMA_Spec.doc */
#ifdef CONFIG_OF
/*for MT6752*/
#define AP_DMA_HIF_BASE             0x11000080
#else
/*for MT6572/82/92*/
#define AP_DMA_HIF_BASE             0x11000180
#endif

#define AP_DMA_HIF_0_INT_FLAG       (0x0000)
#define AP_DMA_HIF_0_INT_EN         (0x0004)
#define AP_DMA_HIF_0_EN             (0x0008)
#define AP_DMA_HIF_0_RST            (0x000C)
#define AP_DMA_HIF_0_STOP           (0x0010)
#define AP_DMA_HIF_0_FLUSH          (0x0014)
#define AP_DMA_HIF_0_CON            (0x0018)
#define AP_DMA_HIF_0_SRC_ADDR       (0x001C)
#define AP_DMA_HIF_0_DST_ADDR       (0x0020)
#define AP_DMA_HIF_0_LEN            (0x0024)
#define AP_DMA_HIF_0_INT_BUF_SIZE   (0x0038)
#define AP_DMA_HIF_0_DEBUG_STATUS   (0x0050)
#define AP_DMA_HIF_0_SRC_ADDR2		(0x0054)
#define AP_DMA_HIF_0_DST_ADDR2		(0x0058)

#define AP_DMA_HIF_0_LENGTH         0x0080

/* AP_DMA_HIF_0_INT_FLAG */
#define ADH_CR_FLAG_0               BIT(0)

/* AP_DMA_HIF_0_INT_EN */
#define ADH_CR_INTEN_FLAG_0         BIT(0)

/* AP_DMA_HIF_0_EN */
#define ADH_CR_EN                   BIT(0)

/* AP_DMA_HIF_0_RST */
#define ADH_CR_HARD_RST             BIT(1)
#define ADH_CR_WARM_RST             BIT(0)

/* AP_DMA_HIF_0_STOP */
#define ADH_CR_PAUSE                BIT(1)
#define ADH_CR_STOP                 BIT(0)

/* AP_DMA_HIF_0_FLUSH */
#define ADH_CR_FLUSH                BIT(0)

/* AP_DMA_HIF_0_CON */
#define ADH_CR_BURST_LEN            BITS(16, 17)
#define ADH_CR_BURST_LEN_OFFSET     16
#define ADH_CR_SLOW_CNT             BITS(5, 14)
#define ADH_CR_SLOW_EN              BIT(2)
#define ADH_CR_FIX_EN               BIT(1)
#define ADH_CR_FIX_EN_OFFSET        1
#define ADH_CR_DIR                  BIT(0)

/* AP_DMA_HIF_0_LEN */
#define ADH_CR_LEN                  BITS(0, 19)

/* AP_DMA_HIF_0_SRC_ADDR2 */
#define ADH_CR_SRC_ADDR2		BIT(0)
/* AP_DMA_HIF_0_DST_ADDR2 */
#define ADH_CR_DST_ADDR2		BIT(0)

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
#endif /* _HIF_PDMA_H */

/* End of hif_gdma.h */
