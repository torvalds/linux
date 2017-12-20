/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __HAL_BTIF_DMA_H_
#define __HAL_BTIF_DMA_H_

#include <linux/io.h>
#include "btif_dma_pub.h"

#if defined(CONFIG_MTK_CLKMGR)
#if defined(CONFIG_ARCH_MT6580)
#define MTK_BTIF_APDMA_CLK_CG MT_CG_APDMA_SW_CG
#elif defined(CONFIG_ARCH_MT6735) || defined(CONFIG_ARCH_MT6735M) || defined(CONFIG_ARCH_MT6753)
#define MTK_BTIF_APDMA_CLK_CG MT_CG_PERI_APDMA
#endif
#else
extern struct clk *clk_btif_apdma; /*btif apdma clock*/
#endif /* !defined(CONFIG_MTK_CLKMGR) */

#define TX_DMA_VFF_SIZE (1024 * 8)	/*Tx vFIFO Len must be 8 Byte allignment */
#define RX_DMA_VFF_SIZE (1024 * 8)	/*Rx vFIFO Len must be 8 Byte allignment */

#define DMA_TX_THRE(n) (n - 7)	/*Tx Trigger Level */
#define DMA_RX_THRE(n) ((n) * 3 / 4)	/*Rx Trigger Level */

/**********************************Hardware related defination**************************/
#ifndef CONFIG_OF
/*DMA channel's offset refer to AP_DMA's base address*/
#define BTIF_TX_DMA_OFFSET 0x880
#define BTIF_RX_DMA_OFFSET 0x900
#endif

/*Register Address Mapping*/
#define DMA_INT_FLAG_OFFSET  0x00
#define DMA_INT_EN_OFFSET  0x04
#define DMA_EN_OFFSET  0x08
#define DMA_RST_OFFSET  0x0C
#define DMA_STOP_OFFSET  0x10
#define DMA_FLUSH_OFFSET  0x14

#define DMA_BASE_OFFSET  0x1C
#define DMA_LEN_OFFSET  0x24

#define DMA_THRE_OFFSET  0x28
#define DMA_WPT_OFFSET  0x2C
#define DMA_RPT_OFFSET  0x30
#define DMA_VALID_OFFSET  0x3C
#define DMA_LEFT_OFFSET  0x40
#define DMA_VFF_BIT29_OFFSET  0x01

#define TX_DMA_INT_FLAG(base)       (unsigned long)(base + 0x0)	/*BTIF Tx Virtual FIFO Interrupt Flag Register */
#define TX_DMA_INT_EN(base)         (unsigned long)(base + 0x4)	/*BTIF Tx Virtual FIFO Interrupt Enable Register */
#define TX_DMA_EN(base)             (unsigned long)(base + DMA_EN_OFFSET)/*BTIF Tx Virtual FIFO Enable Register */
#define TX_DMA_RST(base)            (unsigned long)(base + DMA_RST_OFFSET)/*BTIF Tx Virtual FIFO  Reset Register */
#define TX_DMA_STOP(base)           (unsigned long)(base + DMA_STOP_OFFSET)/*BTIF Tx Virtual FIFO STOP  Register */
#define TX_DMA_FLUSH(base)          (unsigned long)(base + DMA_FLUSH_OFFSET)/*BTIF Tx Virtual FIFO Flush Register */
#define TX_DMA_VFF_ADDR(base)       (unsigned long)(base + 0x1C) /*BTIF Tx Virtual FIFO Base Address Register */
#define TX_DMA_VFF_LEN(base)        (unsigned long)(base + 0x24) /*BTIF Tx Virtual FIFO Length Register */
#define TX_DMA_VFF_THRE(base)       (unsigned long)(base + 0x28) /*BTIF Tx Virtual FIFO Threshold Register */
#define TX_DMA_VFF_WPT(base)        (unsigned long)(base + 0x2C) /*BTIF Tx Virtual FIFO Write Pointer Register */
#define TX_DMA_VFF_RPT(base)        (unsigned long)(base + 0x30) /*BTIF Tx Virtual FIFO Read Pointer  Register */
#define TX_DMA_W_INT_BUF_SIZE(base) (unsigned long)(base + 0x34)
/*BTIF Tx Virtual FIFO Internal Tx Write Buffer Size Register */
#define TX_DMA_INT_BUF_SIZE(base)   (unsigned long)(base + 0x38)
/*BTIF Tx Virtual FIFO Internal Tx Buffer Size Register */

#define TX_DMA_VFF_VALID_SIZE(base) (unsigned long)(base + 0x3C) /*BTIF Tx Virtual FIFO Valid Size Register */
#define TX_DMA_VFF_LEFT_SIZE(base)  (unsigned long)(base + 0x40) /*BTIF Tx Virtual FIFO Left Size Register */
#define TX_DMA_DEBUG_STATUS(base)   (unsigned long)(base + 0x50) /*BTIF Tx Virtual FIFO Debug Status Register */
#define TX_DMA_VFF_ADDR_H(base)     (unsigned long)(base + 0x54) /*BTIF Tx Virtual FIFO Base High Address Register */

/*Rx Register Address Mapping*/
#define RX_DMA_INT_FLAG(base)       (unsigned long)(base + 0x0)	/*BTIF Rx Virtual FIFO Interrupt Flag Register */
#define RX_DMA_INT_EN(base)         (unsigned long)(base + 0x4)	/*BTIF Rx Virtual FIFO Interrupt Enable Register */
#define RX_DMA_EN(base)             (unsigned long)(base + DMA_EN_OFFSET) /*BTIF Rx Virtual FIFO Enable Register */
#define RX_DMA_RST(base)            (unsigned long)(base + DMA_RST_OFFSET) /*BTIF Rx Virtual FIFO Reset Register */
#define RX_DMA_STOP(base)           (unsigned long)(base + DMA_STOP_OFFSET) /*BTIF Rx Virtual FIFO Stop Register */
#define RX_DMA_FLUSH(base)          (unsigned long)(base + DMA_FLUSH_OFFSET) /*BTIF Rx Virtual FIFO Flush Register */
#define RX_DMA_VFF_ADDR(base)       (unsigned long)(base + 0x1C) /*BTIF Rx Virtual FIFO Base Address Register */
#define RX_DMA_VFF_LEN(base)        (unsigned long)(base + 0x24) /*BTIF Rx Virtual FIFO Length Register */
#define RX_DMA_VFF_THRE(base)       (unsigned long)(base + 0x28) /*BTIF Rx Virtual FIFO Threshold Register */
#define RX_DMA_VFF_WPT(base)        (unsigned long)(base + 0x2C) /*BTIF Rx Virtual FIFO Write Pointer Register */
#define RX_DMA_VFF_RPT(base)        (unsigned long)(base + 0x30) /*BTIF Rx Virtual FIFO Read Pointer Register */
#define RX_DMA_FLOW_CTRL_THRE(base) (unsigned long)(base + 0x34) /*BTIF Rx Virtual FIFO Flow Control  Register */
#define RX_DMA_INT_BUF_SIZE(base)   (unsigned long)(base + 0x38) /*BTIF Rx Virtual FIFO Internal Buffer Register */
#define RX_DMA_VFF_VALID_SIZE(base) (unsigned long)(base + 0x3C) /*BTIF Rx Virtual FIFO Valid Size Register */
#define RX_DMA_VFF_LEFT_SIZE(base)  (unsigned long)(base + 0x40) /*BTIF Rx Virtual FIFO Left Size  Register */
#define RX_DMA_DEBUG_STATUS(base)   (unsigned long)(base + 0x50) /*BTIF Rx Virtual FIFO Debug Status Register */
#define RX_DMA_VFF_ADDR_H(base)     (unsigned long)(base + 0x54) /*BTIF Rx Virtual FIFO Base High Address Register */

#define DMA_EN_BIT (0x1)
#define DMA_STOP_BIT (0x1)
#define DMA_RST_BIT (0x1)
#define DMA_FLUSH_BIT (0x1)

#define DMA_WARM_RST (0x1 << 0)
#define DMA_HARD_RST (0x1 << 1)

#define DMA_WPT_MASK (0x0000FFFF)
#define DMA_WPT_WRAP (0x00010000)

#define DMA_RPT_MASK (0x0000FFFF)
#define DMA_RPT_WRAP (0x00010000)

/*APDMA BTIF Tx Reg Ctrl Bit*/
#define TX_DMA_INT_FLAG_MASK (0x1)

#define TX_DMA_INTEN_BIT (0x1)

#define TX_DMA_ADDR_MASK (0xFFFFFFF8)
#define TX_DMA_LEN_MASK (0x0000FFF8)

#define TX_DMA_THRE_MASK (0x0000FFFF)

#define TX_DMA_W_INT_BUF_MASK (0x000000FF)

#define TX_DMA_VFF_VALID_MASK (0x0000FFFF)
#define TX_DMA_VFF_LEFT_MASK (0x0000FFFF)

/*APDMA BTIF Rx Reg Ctrl Bit*/
#define RX_DMA_INT_THRE (0x1 << 0)
#define RX_DMA_INT_DONE (0x1 << 1)

#define RX_DMA_INT_THRE_EN (0x1 << 0)
#define RX_DMA_INT_DONE_EN (0x1 << 1)

#define RX_DMA_ADDR_MASK (0xFFFFFFF8)
#define RX_DMA_LEN_MASK (0x0000FFF8)

#define RX_DMA_THRE_MASK (0x0000FFFF)

#define RX_DMA_FLOW_CTRL_THRE_MASK (0x000000FF)

#define RX_DMA_INT_BUF_SIZE_MASK (0x0000001F)

#define RX_DMA_VFF_VALID_MASK (0x0000001F)

#define RX_DMA_VFF_LEFT_MASK (0x0000FFFF)

typedef struct _MTK_BTIF_DMA_VFIFO_ {
	DMA_VFIFO vfifo;
	unsigned int wpt;	/*DMA's write pointer, which is maintained by SW for Tx DMA and HW for Rx DMA */
	unsigned int last_wpt_wrap;	/*last wrap bit for wpt */
	unsigned int rpt;	/*DMA's read pointer, which is maintained by HW for Tx DMA and SW for Rx DMA */
	unsigned int last_rpt_wrap;	/*last wrap bit for rpt */
} MTK_BTIF_DMA_VFIFO, *P_MTK_BTIF_DMA_VFIFO;

/*for DMA debug purpose*/
typedef struct _MTK_BTIF_DMA_REG_DMP_DBG_ {
	unsigned long reg_addr;
	unsigned int reg_val;
} MTK_BTIF_DMA_REG_DMP_DBG, *P_MTK_BTIF_DMA_REG_DMP_DBG;

#endif /*__HAL_BTIF_DMA_H_*/
