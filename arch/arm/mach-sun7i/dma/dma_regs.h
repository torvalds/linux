/*
 * arch/arm/mach-sun7i/dma/dma_regs.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma regs defination
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __DMA_REGS_H
#define __DMA_REGS_H

/* dma reg offset */
#define DMA_IRQ_EN_REG_OFF            		( 0x0000                        )
#define DMA_IRQ_PEND_REG_OFF            	( 0x0004                        )
#define NDMA_AUTO_GAT_REG_OFF            	( 0x0008                        )
#define N_DMA_CTRL_OFF(chan)            	( 0x100 + ((chan) << 5) + 0x0  )
#define N_DMA_SADR_OFF(chan)            	( 0x100 + ((chan) << 5) + 0x4  )
#define N_DMA_DADR_OFF(chan)            	( 0x100 + ((chan) << 5) + 0x8  )
#define N_DMA_BC_OFF(chan)                      ( 0x100 + ((chan) << 5) + 0xc  )
#define D_DMA_CFG_OFF(chan)            		( 0x300 + (((chan)-8) << 5) + 0x0 )
#define D_DMA_SRC_STADDR_OFF(chan)            	( 0x300 + (((chan)-8) << 5) + 0x4 )
#define D_DMA_DST_STADDR_OFF(chan)            	( 0x300 + (((chan)-8) << 5) + 0x8 )
#define D_DMA_BC_OFF(chan)            		( 0x300 + (((chan)-8) << 5) + 0xc )
#define D_DMA_PARA_OFF(chan)            	( 0x300 + (((chan)-8) << 5) + 0x18)

/* reg offset from channel base */
#define DMA_OFF_REG_CTRL           		( 0x0000                       )
#define DMA_OFF_REG_SADR            		( 0x0004                       )
#define DMA_OFF_REG_DADR            		( 0x0008                       )
#define DMA_OFF_REG_BC            		( 0x000C                       )
#define DMA_OFF_REG_PARA            		( 0x0018                       )

#define IS_DEDICATE(chan)			((chan) >= 8)

/* dma reg addr */
#define DMA_IRQ_EN_REG            		( SW_VA_DMAC_IO_BASE + DMA_IRQ_EN_REG_OFF  	)
#define DMA_IRQ_PEND_REG            		( SW_VA_DMAC_IO_BASE + DMA_IRQ_PEND_REG_OFF  	)
#define NDMA_AUTO_GAT_REG            		( SW_VA_DMAC_IO_BASE + NDMA_AUTO_GAT_REG_OFF  	)
#define DMA_CTRL_REG(chan)            		( SW_VA_DMAC_IO_BASE + (IS_DEDICATE(chan) ? D_DMA_CFG_OFF(chan) : N_DMA_CTRL_OFF(chan)))
#define DMA_SADR_REG(chan)            		( SW_VA_DMAC_IO_BASE + (IS_DEDICATE(chan) ? D_DMA_SRC_STADDR_OFF(chan) : N_DMA_SADR_OFF(chan)))
#define DMA_DADR_REG(chan)            		( SW_VA_DMAC_IO_BASE + (IS_DEDICATE(chan) ? D_DMA_DST_STADDR_OFF(chan) : N_DMA_DADR_OFF(chan)))
#define DMA_BC_REG(chan)                      	( SW_VA_DMAC_IO_BASE + (IS_DEDICATE(chan) ? D_DMA_BC_OFF(chan) : N_DMA_BC_OFF(chan)))
#define DMA_PARA_REG(chan)            		( SW_VA_DMAC_IO_BASE + (IS_DEDICATE(chan) ? D_DMA_PARA_OFF(chan) : ))

/* dma reg rw */
#define DMA_READ_REG(reg)			readl(reg)
#define DMA_WRITE_REG(val, reg)			writel(val, reg)

#endif  /* __DMA_REGS_H */

