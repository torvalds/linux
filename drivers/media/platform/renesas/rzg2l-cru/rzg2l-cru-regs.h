/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rzg2l-cru-regs.h--RZ/G2L (and alike SoCs) CRU Registers Definitions
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RZG2L_CRU_REGS_H__
#define __RZG2L_CRU_REGS_H__

/* HW CRU Registers Definition */

/* CRU Control Register */
#define CRUnCTRL			0x0
#define CRUnCTRL_VINSEL(x)		((x) << 0)

/* CRU Interrupt Enable Register */
#define CRUnIE				0x4
#define CRUnIE_EFE			BIT(17)

/* CRU Interrupt Status Register */
#define CRUnINTS			0x8
#define CRUnINTS_SFS			BIT(16)

/* CRU Reset Register */
#define CRUnRST				0xc
#define CRUnRST_VRESETN			BIT(0)

/* Memory Bank Base Address (Lower) Register for CRU Image Data */
#define AMnMBxADDRL(x)			(0x100 + ((x) * 8))

/* Memory Bank Base Address (Higher) Register for CRU Image Data */
#define AMnMBxADDRH(x)			(0x104 + ((x) * 8))

/* Memory Bank Enable Register for CRU Image Data */
#define AMnMBVALID			0x148
#define AMnMBVALID_MBVALID(x)		GENMASK(x, 0)

/* Memory Bank Status Register for CRU Image Data */
#define AMnMBS				0x14c
#define AMnMBS_MBSTS			0x7

/* AXI Master Transfer Setting Register for CRU Image Data */
#define AMnAXIATTR			0x158
#define AMnAXIATTR_AXILEN_MASK		GENMASK(3, 0)
#define AMnAXIATTR_AXILEN		(0xf)

/* AXI Master FIFO Pointer Register for CRU Image Data */
#define AMnFIFOPNTR			0x168
#define AMnFIFOPNTR_FIFOWPNTR		GENMASK(7, 0)
#define AMnFIFOPNTR_FIFORPNTR_Y		GENMASK(23, 16)

/* AXI Master Transfer Stop Register for CRU Image Data */
#define AMnAXISTP			0x174
#define AMnAXISTP_AXI_STOP		BIT(0)

/* AXI Master Transfer Stop Status Register for CRU Image Data */
#define AMnAXISTPACK			0x178
#define AMnAXISTPACK_AXI_STOP_ACK	BIT(0)

/* CRU Image Processing Enable Register */
#define ICnEN				0x200
#define ICnEN_ICEN			BIT(0)

/* CRU Image Processing Main Control Register */
#define ICnMC				0x208
#define ICnMC_CSCTHR			BIT(5)
#define ICnMC_INF(x)			((x) << 16)
#define ICnMC_VCSEL(x)			((x) << 22)
#define ICnMC_INF_MASK			GENMASK(21, 16)

/* CRU Module Status Register */
#define ICnMS				0x254
#define ICnMS_IA			BIT(2)

/* CRU Data Output Mode Register */
#define ICnDMR				0x26c
#define ICnDMR_YCMODE_UYVY		(1 << 4)

#endif /* __RZG2L_CRU_REGS_H__ */
