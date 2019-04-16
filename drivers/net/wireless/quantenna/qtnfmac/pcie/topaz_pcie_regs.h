/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018 Quantenna Communications */

#ifndef __TOPAZ_PCIE_H
#define __TOPAZ_PCIE_H

/* Topaz PCIe DMA registers */
#define PCIE_DMA_WR_INTR_STATUS(base)		((base) + 0x9bc)
#define PCIE_DMA_WR_INTR_MASK(base)		((base) + 0x9c4)
#define PCIE_DMA_WR_INTR_CLR(base)		((base) + 0x9c8)
#define PCIE_DMA_WR_ERR_STATUS(base)		((base) + 0x9cc)
#define PCIE_DMA_WR_DONE_IMWR_ADDR_LOW(base)	((base) + 0x9D0)
#define PCIE_DMA_WR_DONE_IMWR_ADDR_HIGH(base)	((base) + 0x9d4)

#define PCIE_DMA_RD_INTR_STATUS(base)		((base) + 0x310)
#define PCIE_DMA_RD_INTR_MASK(base)		((base) + 0x319)
#define PCIE_DMA_RD_INTR_CLR(base)		((base) + 0x31c)
#define PCIE_DMA_RD_ERR_STATUS_LOW(base)	((base) + 0x324)
#define PCIE_DMA_RD_ERR_STATUS_HIGH(base)	((base) + 0x328)
#define PCIE_DMA_RD_DONE_IMWR_ADDR_LOW(base)	((base) + 0x33c)
#define PCIE_DMA_RD_DONE_IMWR_ADDR_HIGH(base)	((base) + 0x340)

/* Topaz LHost IPC4 interrupt */
#define TOPAZ_LH_IPC4_INT(base)			((base) + 0x13C)
#define TOPAZ_LH_IPC4_INT_MASK(base)		((base) + 0x140)

#define TOPAZ_RC_TX_DONE_IRQ			(0)
#define TOPAZ_RC_RST_EP_IRQ			(1)
#define TOPAZ_RC_TX_STOP_IRQ			(2)
#define TOPAZ_RC_RX_DONE_IRQ			(3)
#define TOPAZ_RC_PM_EP_IRQ			(4)

/* Topaz LHost M2L interrupt */
#define TOPAZ_CTL_M2L_INT(base)			((base) + 0x2C)
#define TOPAZ_CTL_M2L_INT_MASK(base)		((base) + 0x30)

#define TOPAZ_RC_CTRL_IRQ			(6)

#define TOPAZ_IPC_IRQ_WORD(irq)			(BIT(irq) | BIT(irq + 16))

/* PCIe legacy INTx */
#define TOPAZ_PCIE_CFG0_OFFSET	(0x6C)
#define TOPAZ_ASSERT_INTX	BIT(9)

#endif /* __TOPAZ_PCIE_H */
