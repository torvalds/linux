#ifndef __ASM_SH_RENESAS_RTS7751R2D_H
#define __ASM_SH_RENESAS_RTS7751R2D_H

/*
 * linux/include/asm-sh/renesas_rts7751r2d.h
 *
 * Copyright (C) 2000  Atom Create Engineering Co., Ltd.
 *
 * Renesas Technology Sales RTS7751R2D support
 */

/* Board specific addresses.  */

#define PA_BCR		0xa4000000	/* FPGA */
#define PA_IRLMON	0xa4000002	/* Interrupt Status control */
#define PA_CFCTL	0xa4000004	/* CF Timing control */
#define PA_CFPOW	0xa4000006	/* CF Power control */
#define PA_DISPCTL	0xa4000008	/* Display Timing control */
#define PA_SDMPOW	0xa400000a	/* SD Power control */
#define PA_RTCCE	0xa400000c	/* RTC(9701) Enable control */
#define PA_PCICD	0xa400000e	/* PCI Extension detect control */
#define PA_VOYAGERRTS	0xa4000020	/* VOYAGER Reset control */

#define PA_R2D1_AXRST		0xa4000022	/* AX_LAN Reset control */
#define PA_R2D1_CFRST		0xa4000024	/* CF Reset control */
#define PA_R2D1_ADMRTS		0xa4000026	/* SD Reset control */
#define PA_R2D1_EXTRST		0xa4000028	/* Extension Reset control */
#define PA_R2D1_CFCDINTCLR	0xa400002a	/* CF Insert Interrupt clear */

#define PA_R2DPLUS_CFRST	0xa4000022	/* CF Reset control */
#define PA_R2DPLUS_ADMRTS	0xa4000024	/* SD Reset control */
#define PA_R2DPLUS_EXTRST	0xa4000026	/* Extension Reset control */
#define PA_R2DPLUS_CFCDINTCLR	0xa4000028	/* CF Insert Interrupt clear */
#define PA_R2DPLUS_KEYCTLCLR	0xa400002a	/* Key Interrupt clear */

#define PA_POWOFF	0xa4000030	/* Board Power OFF control */
#define PA_VERREG	0xa4000032	/* FPGA Version Register */
#define PA_INPORT	0xa4000034	/* KEY Input Port control */
#define PA_OUTPORT	0xa4000036	/* LED control */
#define PA_BVERREG	0xa4000038	/* Board Revision Register */

#define PA_AX88796L	0xaa000400	/* AX88796L Area */
#define PA_VOYAGER	0xab000000	/* VOYAGER GX Area */
#define PA_IDE_OFFSET	0x1f0		/* CF IDE Offset */
#define AX88796L_IO_BASE	0x1000	/* AX88796L IO Base Address */

#define IRLCNTR1	(PA_BCR + 0)	/* Interrupt Control Register1 */

#define R2D_FPGA_IRQ_BASE	100

#define IRQ_VOYAGER		(R2D_FPGA_IRQ_BASE + 0)
#define IRQ_EXT			(R2D_FPGA_IRQ_BASE + 1)
#define IRQ_TP			(R2D_FPGA_IRQ_BASE + 2)
#define IRQ_RTC_T		(R2D_FPGA_IRQ_BASE + 3)
#define IRQ_RTC_A		(R2D_FPGA_IRQ_BASE + 4)
#define IRQ_SDCARD		(R2D_FPGA_IRQ_BASE + 5)
#define IRQ_CF_CD		(R2D_FPGA_IRQ_BASE + 6)
#define IRQ_CF_IDE		(R2D_FPGA_IRQ_BASE + 7)
#define IRQ_AX88796		(R2D_FPGA_IRQ_BASE + 8)
#define IRQ_KEY			(R2D_FPGA_IRQ_BASE + 9)
#define IRQ_PCI_INTA		(R2D_FPGA_IRQ_BASE + 10)
#define IRQ_PCI_INTB		(R2D_FPGA_IRQ_BASE + 11)
#define IRQ_PCI_INTC		(R2D_FPGA_IRQ_BASE + 12)
#define IRQ_PCI_INTD		(R2D_FPGA_IRQ_BASE + 13)

/* arch/sh/boards/renesas/rts7751r2d/irq.c */
void init_rts7751r2d_IRQ(void);
int rts7751r2d_irq_demux(int);

#endif  /* __ASM_SH_RENESAS_RTS7751R2D */
