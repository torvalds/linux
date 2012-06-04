#ifndef __ASM_SH_HITACHI_SE7343_H
#define __ASM_SH_HITACHI_SE7343_H

/*
 * include/asm-sh/se/se7343.h
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * SH-Mobile SolutionEngine 7343 support
 */
#include <linux/sh_intc.h>

/* Box specific addresses.  */

/* Area 0 */
#define PA_ROM		0x00000000	/* EPROM */
#define PA_ROM_SIZE	0x00400000	/* EPROM size 4M byte(Actually 2MB) */
#define PA_FROM		0x00400000	/* Flash ROM */
#define PA_FROM_SIZE	0x00400000	/* Flash size 4M byte */
#define PA_SRAM		0x00800000	/* SRAM */
#define PA_FROM_SIZE	0x00400000	/* SRAM size 4M byte */
/* Area 1 */
#define PA_EXT1		0x04000000
#define PA_EXT1_SIZE	0x04000000
/* Area 2 */
#define PA_EXT2		0x08000000
#define PA_EXT2_SIZE	0x04000000
/* Area 3 */
#define PA_SDRAM	0x0c000000
#define PA_SDRAM_SIZE	0x04000000
/* Area 4 */
#define PA_PCIC		0x10000000	/* MR-SHPC-01 PCMCIA */
#define PA_MRSHPC       0xb03fffe0      /* MR-SHPC-01 PCMCIA controller */
#define PA_MRSHPC_MW1   0xb0400000      /* MR-SHPC-01 memory window base */
#define PA_MRSHPC_MW2   0xb0500000      /* MR-SHPC-01 attribute window base */
#define PA_MRSHPC_IO    0xb0600000      /* MR-SHPC-01 I/O window base */
#define MRSHPC_OPTION   (PA_MRSHPC + 6)
#define MRSHPC_CSR      (PA_MRSHPC + 8)
#define MRSHPC_ISR      (PA_MRSHPC + 10)
#define MRSHPC_ICR      (PA_MRSHPC + 12)
#define MRSHPC_CPWCR    (PA_MRSHPC + 14)
#define MRSHPC_MW0CR1   (PA_MRSHPC + 16)
#define MRSHPC_MW1CR1   (PA_MRSHPC + 18)
#define MRSHPC_IOWCR1   (PA_MRSHPC + 20)
#define MRSHPC_MW0CR2   (PA_MRSHPC + 22)
#define MRSHPC_MW1CR2   (PA_MRSHPC + 24)
#define MRSHPC_IOWCR2   (PA_MRSHPC + 26)
#define MRSHPC_CDCR     (PA_MRSHPC + 28)
#define MRSHPC_PCIC_INFO (PA_MRSHPC + 30)
#define PA_LED		0xb0C00000	/* LED */
#define LED_SHIFT       0
#define PA_DIPSW	0xb0900000	/* Dip switch 31 */
#define PA_CPLD_MODESET	0xb1400004	/* CPLD Mode set register */
#define PA_CPLD_ST	0xb1400008	/* CPLD Interrupt status register */
#define PA_CPLD_IMSK	0xb140000a	/* CPLD Interrupt mask register */
/* Area 5 */
#define PA_EXT5		0x14000000
#define PA_EXT5_SIZE	0x04000000
/* Area 6 */
#define PA_LCD1		0xb8000000
#define PA_LCD2		0xb8800000

#define PORT_PACR	0xA4050100
#define PORT_PBCR	0xA4050102
#define PORT_PCCR	0xA4050104
#define PORT_PDCR	0xA4050106
#define PORT_PECR	0xA4050108
#define PORT_PFCR	0xA405010A
#define PORT_PGCR	0xA405010C
#define PORT_PHCR	0xA405010E
#define PORT_PJCR	0xA4050110
#define PORT_PKCR	0xA4050112
#define PORT_PLCR	0xA4050114
#define PORT_PMCR	0xA4050116
#define PORT_PNCR	0xA4050118
#define PORT_PQCR	0xA405011A
#define PORT_PRCR	0xA405011C
#define PORT_PSCR	0xA405011E
#define PORT_PTCR	0xA4050140
#define PORT_PUCR	0xA4050142
#define PORT_PVCR	0xA4050144
#define PORT_PWCR	0xA4050146
#define PORT_PYCR	0xA4050148
#define PORT_PZCR	0xA405014A

#define PORT_PSELA	0xA405014C
#define PORT_PSELB	0xA405014E
#define PORT_PSELC	0xA4050150
#define PORT_PSELD	0xA4050152
#define PORT_PSELE	0xA4050154

#define PORT_HIZCRA	0xA4050156
#define PORT_HIZCRB	0xA4050158
#define PORT_HIZCRC	0xA405015C

#define PORT_DRVCR	0xA4050180

#define PORT_PADR	0xA4050120
#define PORT_PBDR	0xA4050122
#define PORT_PCDR	0xA4050124
#define PORT_PDDR	0xA4050126
#define PORT_PEDR	0xA4050128
#define PORT_PFDR	0xA405012A
#define PORT_PGDR	0xA405012C
#define PORT_PHDR	0xA405012E
#define PORT_PJDR	0xA4050130
#define PORT_PKDR	0xA4050132
#define PORT_PLDR	0xA4050134
#define PORT_PMDR	0xA4050136
#define PORT_PNDR	0xA4050138
#define PORT_PQDR	0xA405013A
#define PORT_PRDR	0xA405013C
#define PORT_PTDR	0xA4050160
#define PORT_PUDR	0xA4050162
#define PORT_PVDR	0xA4050164
#define PORT_PWDR	0xA4050166
#define PORT_PYDR	0xA4050168

#define FPGA_IN		0xb1400000
#define FPGA_OUT	0xb1400002

#define IRQ0_IRQ        evt2irq(0x600)
#define IRQ1_IRQ        evt2irq(0x620)
#define IRQ4_IRQ        evt2irq(0x680)
#define IRQ5_IRQ        evt2irq(0x6a0)

#define SE7343_FPGA_IRQ_MRSHPC0	0
#define SE7343_FPGA_IRQ_MRSHPC1	1
#define SE7343_FPGA_IRQ_MRSHPC2	2
#define SE7343_FPGA_IRQ_MRSHPC3	3
#define SE7343_FPGA_IRQ_SMC	6	/* EXT_IRQ2 */
#define SE7343_FPGA_IRQ_USB	8
#define SE7343_FPGA_IRQ_UARTA	10
#define SE7343_FPGA_IRQ_UARTB	11

#define SE7343_FPGA_IRQ_NR	12

/* arch/sh/boards/se/7343/irq.c */
extern unsigned int se7343_fpga_irq[];

void init_7343se_IRQ(void);

#endif  /* __ASM_SH_HITACHI_SE7343_H */
