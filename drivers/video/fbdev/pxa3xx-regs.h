/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_REGS_LCD_H
#define __ASM_ARCH_REGS_LCD_H

/*
 * LCD Controller Registers and Bits Definitions
 */
#define LCCR0		(0x000)	/* LCD Controller Control Register 0 */
#define LCCR1		(0x004)	/* LCD Controller Control Register 1 */
#define LCCR2		(0x008)	/* LCD Controller Control Register 2 */
#define LCCR3		(0x00C)	/* LCD Controller Control Register 3 */
#define LCCR4		(0x010)	/* LCD Controller Control Register 4 */
#define LCCR5		(0x014)	/* LCD Controller Control Register 5 */
#define LCSR		(0x038)	/* LCD Controller Status Register 0 */
#define LCSR1		(0x034)	/* LCD Controller Status Register 1 */
#define LIIDR		(0x03C)	/* LCD Controller Interrupt ID Register */
#define TMEDRGBR	(0x040)	/* TMED RGB Seed Register */
#define TMEDCR		(0x044)	/* TMED Control Register */

#define FBR0		(0x020)	/* DMA Channel 0 Frame Branch Register */
#define FBR1		(0x024)	/* DMA Channel 1 Frame Branch Register */
#define FBR2		(0x028) /* DMA Channel 2 Frame Branch Register */
#define FBR3		(0x02C) /* DMA Channel 2 Frame Branch Register */
#define FBR4		(0x030) /* DMA Channel 2 Frame Branch Register */
#define FBR5		(0x110) /* DMA Channel 2 Frame Branch Register */
#define FBR6		(0x114) /* DMA Channel 2 Frame Branch Register */

#define OVL1C1		(0x050)	/* Overlay 1 Control Register 1 */
#define OVL1C2		(0x060)	/* Overlay 1 Control Register 2 */
#define OVL2C1		(0x070)	/* Overlay 2 Control Register 1 */
#define OVL2C2		(0x080)	/* Overlay 2 Control Register 2 */

#define CMDCR		(0x100)	/* Command Control Register */
#define PRSR		(0x104)	/* Panel Read Status Register */

#define LCCR3_BPP(x)	((((x) & 0x7) << 24) | (((x) & 0x8) ? (1 << 29) : 0))

#define LCCR3_PDFOR_0	(0 << 30)
#define LCCR3_PDFOR_1	(1 << 30)
#define LCCR3_PDFOR_2	(2 << 30)
#define LCCR3_PDFOR_3	(3 << 30)

#define LCCR4_PAL_FOR_0	(0 << 15)
#define LCCR4_PAL_FOR_1	(1 << 15)
#define LCCR4_PAL_FOR_2	(2 << 15)
#define LCCR4_PAL_FOR_3	(3 << 15)
#define LCCR4_PAL_FOR_MASK	(3 << 15)

#define FDADR0		(0x200)	/* DMA Channel 0 Frame Descriptor Address Register */
#define FDADR1		(0x210)	/* DMA Channel 1 Frame Descriptor Address Register */
#define FDADR2		(0x220)	/* DMA Channel 2 Frame Descriptor Address Register */
#define FDADR3		(0x230)	/* DMA Channel 3 Frame Descriptor Address Register */
#define FDADR4		(0x240)	/* DMA Channel 4 Frame Descriptor Address Register */
#define FDADR5		(0x250)	/* DMA Channel 5 Frame Descriptor Address Register */
#define FDADR6		(0x260) /* DMA Channel 6 Frame Descriptor Address Register */

#define LCCR0_ENB	(1 << 0)	/* LCD Controller enable */
#define LCCR0_CMS	(1 << 1)	/* Color/Monochrome Display Select */
#define LCCR0_Color	(LCCR0_CMS*0)	/*  Color display */
#define LCCR0_Mono	(LCCR0_CMS*1)	/*  Monochrome display */
#define LCCR0_SDS	(1 << 2)	/* Single/Dual Panel Display Select */
#define LCCR0_Sngl	(LCCR0_SDS*0)	/*  Single panel display */
#define LCCR0_Dual	(LCCR0_SDS*1)	/*  Dual panel display */

#define LCCR0_LDM	(1 << 3)	/* LCD Disable Done Mask */
#define LCCR0_SFM	(1 << 4)	/* Start of frame mask */
#define LCCR0_IUM	(1 << 5)	/* Input FIFO underrun mask */
#define LCCR0_EFM	(1 << 6)	/* End of Frame mask */
#define LCCR0_PAS	(1 << 7)	/* Passive/Active display Select */
#define LCCR0_Pas	(LCCR0_PAS*0)	/*  Passive display (STN) */
#define LCCR0_Act	(LCCR0_PAS*1)	/*  Active display (TFT) */
#define LCCR0_DPD	(1 << 9)	/* Double Pixel Data (monochrome) */
#define LCCR0_4PixMono	(LCCR0_DPD*0)	/*  4-Pixel/clock Monochrome display */
#define LCCR0_8PixMono	(LCCR0_DPD*1)	/*  8-Pixel/clock Monochrome display */
#define LCCR0_DIS	(1 << 10)	/* LCD Disable */
#define LCCR0_QDM	(1 << 11)	/* LCD Quick Disable mask */
#define LCCR0_PDD	(0xff << 12)	/* Palette DMA request delay */
#define LCCR0_PDD_S	12
#define LCCR0_BM	(1 << 20)	/* Branch mask */
#define LCCR0_OUM	(1 << 21)	/* Output FIFO underrun mask */
#define LCCR0_LCDT	(1 << 22)	/* LCD panel type */
#define LCCR0_RDSTM	(1 << 23)	/* Read status interrupt mask */
#define LCCR0_CMDIM	(1 << 24)	/* Command interrupt mask */
#define LCCR0_OUC	(1 << 25)	/* Overlay Underlay control bit */
#define LCCR0_LDDALT	(1 << 26)	/* LDD alternate mapping control */

#define Fld(Size, Shft)	(((Size) << 16) + (Shft))
#define FShft(Field)	((Field) & 0x0000FFFF)

#define LCCR1_PPL	Fld (10, 0)	/* Pixels Per Line - 1 */
#define LCCR1_DisWdth(Pixel)	(((Pixel) - 1) << FShft (LCCR1_PPL))

#define LCCR1_HSW	Fld (6, 10)	/* Horizontal Synchronization */
#define LCCR1_HorSnchWdth(Tpix)	(((Tpix) - 1) << FShft (LCCR1_HSW))

#define LCCR1_ELW	Fld (8, 16)	/* End-of-Line pixel clock Wait - 1 */
#define LCCR1_EndLnDel(Tpix)	(((Tpix) - 1) << FShft (LCCR1_ELW))

#define LCCR1_BLW	Fld (8, 24)	/* Beginning-of-Line pixel clock */
#define LCCR1_BegLnDel(Tpix)	(((Tpix) - 1) << FShft (LCCR1_BLW))

#define LCCR2_LPP	Fld (10, 0)	/* Line Per Panel - 1 */
#define LCCR2_DisHght(Line)	(((Line) - 1) << FShft (LCCR2_LPP))

#define LCCR2_VSW	Fld (6, 10)	/* Vertical Synchronization pulse - 1 */
#define LCCR2_VrtSnchWdth(Tln)	(((Tln) - 1) << FShft (LCCR2_VSW))

#define LCCR2_EFW	Fld (8, 16)	/* End-of-Frame line clock Wait */
#define LCCR2_EndFrmDel(Tln)	((Tln) << FShft (LCCR2_EFW))

#define LCCR2_BFW	Fld (8, 24)	/* Beginning-of-Frame line clock */
#define LCCR2_BegFrmDel(Tln)	((Tln) << FShft (LCCR2_BFW))

#define LCCR3_API	(0xf << 16)	/* AC Bias pin trasitions per interrupt */
#define LCCR3_API_S	16
#define LCCR3_VSP	(1 << 20)	/* vertical sync polarity */
#define LCCR3_HSP	(1 << 21)	/* horizontal sync polarity */
#define LCCR3_PCP	(1 << 22)	/* Pixel Clock Polarity (L_PCLK) */
#define LCCR3_PixRsEdg	(LCCR3_PCP*0)	/*  Pixel clock Rising-Edge */
#define LCCR3_PixFlEdg	(LCCR3_PCP*1)	/*  Pixel clock Falling-Edge */

#define LCCR3_OEP	(1 << 23)	/* Output Enable Polarity */
#define LCCR3_OutEnH	(LCCR3_OEP*0)	/*  Output Enable active High */
#define LCCR3_OutEnL	(LCCR3_OEP*1)	/*  Output Enable active Low */

#define LCCR3_DPC	(1 << 27)	/* double pixel clock mode */
#define LCCR3_PCD	Fld (8, 0)	/* Pixel Clock Divisor */
#define LCCR3_PixClkDiv(Div)	(((Div) << FShft (LCCR3_PCD)))

#define LCCR3_ACB	Fld (8, 8)	/* AC Bias */
#define LCCR3_Acb(Acb)	(((Acb) << FShft (LCCR3_ACB)))

#define LCCR3_HorSnchH	(LCCR3_HSP*0)	/*  HSP Active High */
#define LCCR3_HorSnchL	(LCCR3_HSP*1)	/*  HSP Active Low */

#define LCCR3_VrtSnchH	(LCCR3_VSP*0)	/*  VSP Active High */
#define LCCR3_VrtSnchL	(LCCR3_VSP*1)	/*  VSP Active Low */

#define LCCR5_IUM(x)	(1 << ((x) + 23)) /* input underrun mask */
#define LCCR5_BSM(x)	(1 << ((x) + 15)) /* branch mask */
#define LCCR5_EOFM(x)	(1 << ((x) + 7))  /* end of frame mask */
#define LCCR5_SOFM(x)	(1 << ((x) + 0))  /* start of frame mask */

#define LCSR_LDD	(1 << 0)	/* LCD Disable Done */
#define LCSR_SOF	(1 << 1)	/* Start of frame */
#define LCSR_BER	(1 << 2)	/* Bus error */
#define LCSR_ABC	(1 << 3)	/* AC Bias count */
#define LCSR_IUL	(1 << 4)	/* input FIFO underrun Lower panel */
#define LCSR_IUU	(1 << 5)	/* input FIFO underrun Upper panel */
#define LCSR_OU		(1 << 6)	/* output FIFO underrun */
#define LCSR_QD		(1 << 7)	/* quick disable */
#define LCSR_EOF	(1 << 8)	/* end of frame */
#define LCSR_BS		(1 << 9)	/* branch status */
#define LCSR_SINT	(1 << 10)	/* subsequent interrupt */
#define LCSR_RD_ST	(1 << 11)	/* read status */
#define LCSR_CMD_INT	(1 << 12)	/* command interrupt */

#define LCSR1_IU(x)	(1 << ((x) + 23)) /* Input FIFO underrun */
#define LCSR1_BS(x)	(1 << ((x) + 15)) /* Branch Status */
#define LCSR1_EOF(x)	(1 << ((x) + 7))  /* End of Frame Status */
#define LCSR1_SOF(x)	(1 << ((x) - 1))  /* Start of Frame Status */

#define LDCMD_PAL	(1 << 26)	/* instructs DMA to load palette buffer */

/* overlay control registers */
#define OVLxC1_PPL(x)	((((x) - 1) & 0x3ff) << 0)	/* Pixels Per Line */
#define OVLxC1_LPO(x)	((((x) - 1) & 0x3ff) << 10)	/* Number of Lines */
#define OVLxC1_BPP(x)	(((x) & 0xf) << 20)	/* Bits Per Pixel */
#define OVLxC1_OEN	(1 << 31)		/* Enable bit for Overlay */
#define OVLxC2_XPOS(x)	(((x) & 0x3ff) << 0)	/* Horizontal Position */
#define OVLxC2_YPOS(x)	(((x) & 0x3ff) << 10)	/* Vertical Position */
#define OVL2C2_PFOR(x)	(((x) & 0x7) << 20)	/* Pixel Format */

/* smartpanel related */
#define PRSR_DATA(x)	((x) & 0xff)	/* Panel Data */
#define PRSR_A0		(1 << 8)	/* Read Data Source */
#define PRSR_ST_OK	(1 << 9)	/* Status OK */
#define PRSR_CON_NT	(1 << 10)	/* Continue to Next Command */

#endif /* __ASM_ARCH_REGS_LCD_H */
