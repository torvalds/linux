/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rzg2l-cru-regs.h--RZ/G2L (and alike SoCs) CRU Registers Definitions
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RZG2L_CRU_REGS_H__
#define __RZG2L_CRU_REGS_H__

/* HW CRU Registers Definition */

#define CRUnCTRL_VINSEL(x)		((x) << 0)

#define CRUnIE_EFE			BIT(17)

#define CRUnIE2_FSxE(x)			BIT(((x) * 3))
#define CRUnIE2_FExE(x)			BIT(((x) * 3) + 1)

#define CRUnINTS_SFS			BIT(16)

#define CRUnINTS2_FSxS(x)		BIT(((x) * 3))

#define CRUnRST_VRESETN			BIT(0)

/* Memory Bank Base Address (Lower) Register for CRU Image Data */
#define AMnMBxADDRL(x)			(AMnMB1ADDRL + (x) * 2)

/* Memory Bank Base Address (Higher) Register for CRU Image Data */
#define AMnMBxADDRH(x)			(AMnMB1ADDRH + (x) * 2)

#define AMnMBVALID_MBVALID(x)		GENMASK(x, 0)

#define AMnMBS_MBSTS			0x7

#define AMnAXIATTR_AXILEN_MASK		GENMASK(3, 0)
#define AMnAXIATTR_AXILEN		(0xf)

#define AMnFIFOPNTR_FIFOWPNTR		GENMASK(7, 0)
#define AMnFIFOPNTR_FIFOWPNTR_B0	AMnFIFOPNTR_FIFOWPNTR
#define AMnFIFOPNTR_FIFOWPNTR_B1	GENMASK(15, 8)
#define AMnFIFOPNTR_FIFORPNTR_Y		GENMASK(23, 16)
#define AMnFIFOPNTR_FIFORPNTR_B0	AMnFIFOPNTR_FIFORPNTR_Y
#define AMnFIFOPNTR_FIFORPNTR_B1	GENMASK(31, 24)

#define AMnIS_IS_MASK			GENMASK(14, 7)
#define AMnIS_IS(x)			((x) << 7)

#define AMnAXISTP_AXI_STOP		BIT(0)

#define AMnAXISTPACK_AXI_STOP_ACK	BIT(0)

#define ICnEN_ICEN			BIT(0)

#define ICnSVC_SVC0(x)			(x)
#define ICnSVC_SVC1(x)			((x) << 4)
#define ICnSVC_SVC2(x)			((x) << 8)
#define ICnSVC_SVC3(x)			((x) << 12)

#define ICnMC_CSCTHR			BIT(5)
#define ICnMC_INF(x)			((x) << 16)
#define ICnMC_VCSEL(x)			((x) << 22)
#define ICnMC_INF_MASK			GENMASK(21, 16)

#define ICnMS_IA			BIT(2)

#define ICnDMR_YCMODE_UYVY		(1 << 4)

enum rzg2l_cru_common_regs {
	CRUnCTRL,	/* CRU Control */
	CRUnIE,		/* CRU Interrupt Enable */
	CRUnIE2,	/* CRU Interrupt Enable(2) */
	CRUnINTS,	/* CRU Interrupt Status */
	CRUnINTS2,	/* CRU Interrupt Status(2) */
	CRUnRST,	/* CRU Reset */
	AMnMB1ADDRL,	/* Bank 1 Address (Lower) for CRU Image Data */
	AMnMB1ADDRH,	/* Bank 1 Address (Higher) for CRU Image Data */
	AMnMB2ADDRL,    /* Bank 2 Address (Lower) for CRU Image Data */
	AMnMB2ADDRH,    /* Bank 2 Address (Higher) for CRU Image Data */
	AMnMB3ADDRL,    /* Bank 3 Address (Lower) for CRU Image Data */
	AMnMB3ADDRH,    /* Bank 3 Address (Higher) for CRU Image Data */
	AMnMB4ADDRL,    /* Bank 4 Address (Lower) for CRU Image Data */
	AMnMB4ADDRH,    /* Bank 4 Address (Higher) for CRU Image Data */
	AMnMB5ADDRL,    /* Bank 5 Address (Lower) for CRU Image Data */
	AMnMB5ADDRH,    /* Bank 5 Address (Higher) for CRU Image Data */
	AMnMB6ADDRL,    /* Bank 6 Address (Lower) for CRU Image Data */
	AMnMB6ADDRH,    /* Bank 6 Address (Higher) for CRU Image Data */
	AMnMB7ADDRL,    /* Bank 7 Address (Lower) for CRU Image Data */
	AMnMB7ADDRH,    /* Bank 7 Address (Higher) for CRU Image Data */
	AMnMB8ADDRL,    /* Bank 8 Address (Lower) for CRU Image Data */
	AMnMB8ADDRH,    /* Bank 8 Address (Higher) for CRU Image Data */
	AMnMBVALID,	/* Memory Bank Enable for CRU Image Data */
	AMnMBS,		/* Memory Bank Status for CRU Image Data */
	AMnMADRSL,	/* VD Memory Address Lower Status Register */
	AMnMADRSH,	/* VD Memory Address Higher Status Register */
	AMnAXIATTR,	/* AXI Master Transfer Setting Register for CRU Image Data */
	AMnFIFOPNTR,	/* AXI Master FIFO Pointer for CRU Image Data */
	AMnAXISTP,	/* AXI Master Transfer Stop for CRU Image Data */
	AMnAXISTPACK,	/* AXI Master Transfer Stop Status for CRU Image Data */
	AMnIS,		/* Image Stride Setting Register */
	ICnEN,		/* CRU Image Processing Enable */
	ICnSVCNUM,	/* CRU SVC Number Register */
	ICnSVC,		/* CRU VC Select Register */
	ICnMC,		/* CRU Image Processing Main Control */
	ICnIPMC_C0,	/* CRU Image Converter Main Control 0 */
	ICnMS,		/* CRU Module Status */
	ICnDMR,		/* CRU Data Output Mode */
	RZG2L_CRU_MAX_REG,
};

#endif /* __RZG2L_CRU_REGS_H__ */
