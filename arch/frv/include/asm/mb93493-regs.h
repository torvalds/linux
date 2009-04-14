/* mb93493-regs.h: MB93493 companion chip registers
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MB93493_REGS_H
#define _ASM_MB93493_REGS_H

#include <asm/mb-regs.h>
#include <asm/mb93493-irqs.h>

#define __addr_MB93493(X)	((volatile unsigned long *)(__region_CS3 + (X)))
#define __get_MB93493(X)	({ *(volatile unsigned long *)(__region_CS3 + (X)); })

#define __set_MB93493(X,V)						\
do {									\
	*(volatile unsigned long *)(__region_CS3 + (X)) = (V); mb();	\
} while(0)

#define __get_MB93493_STSR(X)	__get_MB93493(0x3c0 + (X) * 4)
#define __set_MB93493_STSR(X,V)	__set_MB93493(0x3c0 + (X) * 4, (V))
#define MB93493_STSR_EN

#define __addr_MB93493_IQSR(X)	__addr_MB93493(0x3d0 + (X) * 4)
#define __get_MB93493_IQSR(X)	__get_MB93493(0x3d0 + (X) * 4)
#define __set_MB93493_IQSR(X,V)	__set_MB93493(0x3d0 + (X) * 4, (V))

#define __get_MB93493_DQSR(X)	__get_MB93493(0x3e0 + (X) * 4)
#define __set_MB93493_DQSR(X,V)	__set_MB93493(0x3e0 + (X) * 4, (V))

#define __get_MB93493_LBSER()	__get_MB93493(0x3f0)
#define __set_MB93493_LBSER(V)	__set_MB93493(0x3f0, (V))

#define MB93493_LBSER_VDC	0x00010000
#define MB93493_LBSER_VCC	0x00020000
#define MB93493_LBSER_AUDIO	0x00040000
#define MB93493_LBSER_I2C_0	0x00080000
#define MB93493_LBSER_I2C_1	0x00100000
#define MB93493_LBSER_USB	0x00200000
#define MB93493_LBSER_GPIO	0x00800000
#define MB93493_LBSER_PCMCIA	0x01000000

#define __get_MB93493_LBSR()	__get_MB93493(0x3fc)
#define __set_MB93493_LBSR(V)	__set_MB93493(0x3fc, (V))

/*
 * video display controller
 */
#define __get_MB93493_VDC(X)	__get_MB93493(MB93493_VDC_##X)
#define __set_MB93493_VDC(X,V)	__set_MB93493(MB93493_VDC_##X, (V))

#define MB93493_VDC_RCURSOR	0x140	/* cursor position */
#define MB93493_VDC_RCT1	0x144	/* cursor colour 1 */
#define MB93493_VDC_RCT2	0x148	/* cursor colour 2 */
#define MB93493_VDC_RHDC	0x150	/* horizontal display period */
#define MB93493_VDC_RH_MARGINS	0x154	/* horizontal margin sizes */
#define MB93493_VDC_RVDC	0x158	/* vertical display period */
#define MB93493_VDC_RV_MARGINS	0x15c	/* vertical margin sizes */
#define MB93493_VDC_RC		0x170	/* VDC control */
#define MB93493_VDC_RCLOCK	0x174	/* clock divider, DMA req delay */
#define MB93493_VDC_RBLACK	0x178	/* black insert sizes */
#define MB93493_VDC_RS		0x17c	/* VDC status */

#define __addr_MB93493_VDC_BCI(X)  ({ (volatile unsigned long *)(__region_CS3 + 0x000 + (X)); })
#define __addr_MB93493_VDC_TPO(X)  (__region_CS3 + 0x1c0 + (X))

#define VDC_TPO_WIDTH		32

#define VDC_RC_DSR		0x00000080	/* VDC master reset */

#define VDC_RS_IT		0x00060000	/* interrupt indicators */
#define VDC_RS_IT_UNDERFLOW	0x00040000	/* - underflow event */
#define VDC_RS_IT_VSYNC		0x00020000	/* - VSYNC event */
#define VDC_RS_DFI		0x00010000	/* current interlace field number */
#define VDC_RS_DFI_TOP		0x00000000	/* - top field */
#define VDC_RS_DFI_BOTTOM	0x00010000	/* - bottom field */
#define VDC_RS_DCSR		0x00000010	/* cursor state */
#define VDC_RS_DCM		0x00000003	/* display mode */
#define VDC_RS_DCM_DISABLED	0x00000000	/* - display disabled */
#define VDC_RS_DCM_STOPPED	0x00000001	/* - VDC stopped */
#define VDC_RS_DCM_FREERUNNING	0x00000002	/* - VDC free-running */
#define VDC_RS_DCM_TRANSFERRING	0x00000003	/* - data being transferred to VDC */

/*
 * video capture controller
 */
#define __get_MB93493_VCC(X)	__get_MB93493(MB93493_VCC_##X)
#define __set_MB93493_VCC(X,V)	__set_MB93493(MB93493_VCC_##X, (V))

#define MB93493_VCC_RREDUCT	0x104	/* reduction rate */
#define MB93493_VCC_RHY		0x108	/* horizontal brightness filter coefficients */
#define MB93493_VCC_RHC		0x10c	/* horizontal colour-difference filter coefficients */
#define MB93493_VCC_RHSIZE	0x110	/* horizontal cycle sizes */
#define MB93493_VCC_RHBC	0x114	/* horizontal back porch size */
#define MB93493_VCC_RVCC	0x118	/* vertical capture period */
#define MB93493_VCC_RVBC	0x11c	/* vertical back porch period */
#define MB93493_VCC_RV		0x120	/* vertical filter coefficients */
#define MB93493_VCC_RDTS	0x128	/* DMA transfer size */
#define MB93493_VCC_RDTS_4B	0x01000000	/* 4-byte transfer */
#define MB93493_VCC_RDTS_32B	0x03000000	/* 32-byte transfer */
#define MB93493_VCC_RDTS_SHIFT	24
#define MB93493_VCC_RCC		0x130	/* VCC control */
#define MB93493_VCC_RIS		0x134	/* VCC interrupt status */

#define __addr_MB93493_VCC_TPI(X)  (__region_CS3 + 0x180 + (X))

#define VCC_RHSIZE_RHCC		0x000007ff
#define VCC_RHSIZE_RHCC_SHIFT	0
#define VCC_RHSIZE_RHTCC	0x0fff0000
#define VCC_RHSIZE_RHTCC_SHIFT	16

#define VCC_RVBC_RVBC		0x00003f00
#define VCC_RVBC_RVBC_SHIFT	8

#define VCC_RREDUCT_RHR		0x07ff0000
#define VCC_RREDUCT_RHR_SHIFT	16
#define VCC_RREDUCT_RVR		0x000007ff
#define VCC_RREDUCT_RVR_SHIFT	0

#define VCC_RCC_CE		0x00000001	/* VCC enable */
#define VCC_RCC_CS		0x00000002	/* request video capture start */
#define VCC_RCC_CPF		0x0000000c	/* pixel format */
#define VCC_RCC_CPF_YCBCR_16	0x00000000	/* - YCbCr 4:2:2 16-bit format */
#define VCC_RCC_CPF_RGB		0x00000004	/* - RGB 4:4:4 format */
#define VCC_RCC_CPF_YCBCR_24	0x00000008	/* - YCbCr 4:2:2 24-bit format */
#define VCC_RCC_CPF_BT656	0x0000000c	/* - ITU R-BT.656 format */
#define VCC_RCC_CPF_SHIFT	2
#define VCC_RCC_CSR		0x00000080	/* request reset */
#define VCC_RCC_HSIP		0x00000100	/* HSYNC polarity */
#define VCC_RCC_HSIP_LOACT	0x00000000	/* - low active */
#define VCC_RCC_HSIP_HIACT	0x00000100	/* - high active */
#define VCC_RCC_VSIP		0x00000200	/* VSYNC polarity */
#define VCC_RCC_VSIP_LOACT	0x00000000	/* - low active */
#define VCC_RCC_VSIP_HIACT	0x00000200	/* - high active */
#define VCC_RCC_CIE		0x00000800	/* interrupt enable */
#define VCC_RCC_CFP		0x00001000	/* RGB pixel packing */
#define VCC_RCC_CFP_4TO3	0x00000000	/* - pack 4 pixels into 3 words */
#define VCC_RCC_CFP_1TO1	0x00001000	/* - pack 1 pixel into 1 words */
#define VCC_RCC_CSM		0x00006000	/* interlace specification */
#define VCC_RCC_CSM_ONEPASS	0x00002000	/* - non-interlaced */
#define VCC_RCC_CSM_INTERLACE	0x00004000	/* - interlaced */
#define VCC_RCC_CSM_SHIFT	13
#define VCC_RCC_ES		0x00008000	/* capture start polarity */
#define VCC_RCC_ES_NEG		0x00000000	/* - negative edge */
#define VCC_RCC_ES_POS		0x00008000	/* - positive edge */
#define VCC_RCC_IFI		0x00080000	/* inferlace field evaluation reverse */
#define VCC_RCC_FDTS		0x00300000	/* interlace field start */
#define VCC_RCC_FDTS_3_8	0x00000000	/* - 3/8 of horizontal entire cycle */
#define VCC_RCC_FDTS_1_4	0x00100000	/* - 1/4 of horizontal entire cycle */
#define VCC_RCC_FDTS_7_16	0x00200000	/* - 7/16 of horizontal entire cycle */
#define VCC_RCC_FDTS_SHIFT	20
#define VCC_RCC_MOV		0x00400000	/* test bit - always set to 1 */
#define VCC_RCC_STP		0x00800000	/* request video capture stop */
#define VCC_RCC_TO		0x01000000	/* input during top-field only */

#define VCC_RIS_VSYNC		0x01000000	/* VSYNC interrupt */
#define VCC_RIS_OV		0x02000000	/* overflow interrupt */
#define VCC_RIS_BOTTOM		0x08000000	/* interlace bottom field */
#define VCC_RIS_STARTED		0x10000000	/* capture started */

/*
 * I2C
 */
#define MB93493_I2C_BSR 	0x340		/* bus status */
#define MB93493_I2C_BCR		0x344		/* bus control */
#define MB93493_I2C_CCR		0x348		/* clock control */
#define MB93493_I2C_ADR		0x34c		/* address */
#define MB93493_I2C_DTR		0x350		/* data */
#define MB93493_I2C_BC2R	0x35c		/* bus control 2 */

#define __addr_MB93493_I2C(port,X)   (__region_CS3 + MB93493_I2C_##X + ((port)*0x20))
#define __get_MB93493_I2C(port,X)    __get_MB93493(MB93493_I2C_##X + ((port)*0x20))
#define __set_MB93493_I2C(port,X,V)  __set_MB93493(MB93493_I2C_##X + ((port)*0x20), (V))

#define I2C_BSR_BB	(1 << 7)

/*
 * audio controller (I2S) registers
 */
#define __get_MB93493_I2S(X)	__get_MB93493(MB93493_I2S_##X)
#define __set_MB93493_I2S(X,V)	__set_MB93493(MB93493_I2S_##X, (V))

#define MB93493_I2S_ALDR	0x300		/* L-channel data */
#define MB93493_I2S_ARDR	0x304		/* R-channel data */
#define MB93493_I2S_APDR	0x308		/* 16-bit packed data */
#define MB93493_I2S_AISTR	0x310		/* status */
#define MB93493_I2S_AICR	0x314		/* control */

#define __addr_MB93493_I2S_ALDR(X)	(__region_CS3 + MB93493_I2S_ALDR + (X))
#define __addr_MB93493_I2S_ARDR(X)	(__region_CS3 + MB93493_I2S_ARDR + (X))
#define __addr_MB93493_I2S_APDR(X)	(__region_CS3 + MB93493_I2S_APDR + (X))
#define __addr_MB93493_I2S_ADR(X)	(__region_CS3 + 0x320 + (X))

#define I2S_AISTR_OTST		0x00000003	/* status of output data transfer */
#define I2S_AISTR_OTR		0x00000010	/* output transfer request pending */
#define I2S_AISTR_OUR		0x00000020	/* output FIFO underrun detected */
#define I2S_AISTR_OOR		0x00000040	/* output FIFO overrun detected */
#define I2S_AISTR_ODS		0x00000100	/* output DMA transfer size */
#define I2S_AISTR_ODE		0x00000400	/* output DMA transfer request enable */
#define I2S_AISTR_OTRIE		0x00001000	/* output transfer request interrupt enable */
#define I2S_AISTR_OURIE		0x00002000	/* output FIFO underrun interrupt enable */
#define I2S_AISTR_OORIE		0x00004000	/* output FIFO overrun interrupt enable */
#define I2S_AISTR__OUT_MASK	0x00007570
#define I2S_AISTR_ITST		0x00030000	/* status of input data transfer */
#define I2S_AISTR_ITST_SHIFT	16
#define I2S_AISTR_ITR		0x00100000	/* input transfer request pending */
#define I2S_AISTR_IUR		0x00200000	/* input FIFO underrun detected */
#define I2S_AISTR_IOR		0x00400000	/* input FIFO overrun detected */
#define I2S_AISTR_IDS		0x01000000	/* input DMA transfer size */
#define I2S_AISTR_IDE		0x04000000	/* input DMA transfer request enable */
#define I2S_AISTR_ITRIE		0x10000000	/* input transfer request interrupt enable */
#define I2S_AISTR_IURIE		0x20000000	/* input FIFO underrun interrupt enable */
#define I2S_AISTR_IORIE		0x40000000	/* input FIFO overrun interrupt enable */
#define I2S_AISTR__IN_MASK	0x75700000

#define I2S_AICR_MI		0x00000001	/* mono input requested */
#define I2S_AICR_AMI		0x00000002	/* relation between LRCKI/FS1 and SDI */
#define I2S_AICR_LRI		0x00000004	/* function of LRCKI pin */
#define I2S_AICR_SDMI		0x00000070	/* format of input audio data */
#define I2S_AICR_SDMI_SHIFT	4
#define I2S_AICR_CLI		0x00000080	/* input FIFO clearing control */
#define I2S_AICR_IM		0x00000300	/* input state control */
#define I2S_AICR_IM_SHIFT	8
#define I2S_AICR__IN_MASK	0x000003f7
#define I2S_AICR_MO		0x00001000	/* mono output requested */
#define I2S_AICR_AMO		0x00002000	/* relation between LRCKO/FS0 and SDO */
#define I2S_AICR_AMO_SHIFT	13
#define I2S_AICR_LRO		0x00004000	/* function of LRCKO pin */
#define I2S_AICR_SDMO		0x00070000	/* format of output audio data */
#define I2S_AICR_SDMO_SHIFT	16
#define I2S_AICR_CLO		0x00080000	/* output FIFO clearing control */
#define I2S_AICR_OM		0x00100000	/* output state control */
#define I2S_AICR__OUT_MASK	0x001f7000
#define I2S_AICR_DIV		0x03000000	/* frequency division rate */
#define I2S_AICR_DIV_SHIFT	24
#define I2S_AICR_FL		0x20000000	/* frame length */
#define I2S_AICR_FS		0x40000000	/* frame sync method */
#define I2S_AICR_ME		0x80000000	/* master enable */

/*
 * PCMCIA
 */
#define __addr_MB93493_PCMCIA(X)  ((volatile unsigned long *)(__region_CS5 + (X)))

/*
 * GPIO
 */
#define __get_MB93493_GPIO_PDR(X)	__get_MB93493(0x380 + (X) * 0xc0)
#define __set_MB93493_GPIO_PDR(X,V)	__set_MB93493(0x380 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_GPDR(X)	__get_MB93493(0x384 + (X) * 0xc0)
#define __set_MB93493_GPIO_GPDR(X,V)	__set_MB93493(0x384 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_SIR(X)	__get_MB93493(0x388 + (X) * 0xc0)
#define __set_MB93493_GPIO_SIR(X,V)	__set_MB93493(0x388 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_SOR(X)	__get_MB93493(0x38c + (X) * 0xc0)
#define __set_MB93493_GPIO_SOR(X,V)	__set_MB93493(0x38c + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_PDSR(X)	__get_MB93493(0x390 + (X) * 0xc0)
#define __set_MB93493_GPIO_PDSR(X,V)	__set_MB93493(0x390 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_PDCR(X)	__get_MB93493(0x394 + (X) * 0xc0)
#define __set_MB93493_GPIO_PDCR(X,V)	__set_MB93493(0x394 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_INTST(X)	__get_MB93493(0x398 + (X) * 0xc0)
#define __set_MB93493_GPIO_INTST(X,V)	__set_MB93493(0x398 + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_IEHL(X)	__get_MB93493(0x39c + (X) * 0xc0)
#define __set_MB93493_GPIO_IEHL(X,V)	__set_MB93493(0x39c + (X) * 0xc0, (V))

#define __get_MB93493_GPIO_IELH(X)	__get_MB93493(0x3a0 + (X) * 0xc0)
#define __set_MB93493_GPIO_IELH(X,V)	__set_MB93493(0x3a0 + (X) * 0xc0, (V))

#endif /* _ASM_MB93493_REGS_H */
