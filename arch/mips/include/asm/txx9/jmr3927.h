/*
 * Defines for the TJSYS JMR-TX3927
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 */
#ifndef __ASM_TXX9_JMR3927_H
#define __ASM_TXX9_JMR3927_H

#include <asm/txx9/tx3927.h>
#include <asm/addrspace.h>
#include <asm/txx9irq.h>

/* CS */
#define JMR3927_ROMCE0	0x1fc00000	/* 4M */
#define JMR3927_ROMCE1	0x1e000000	/* 4M */
#define JMR3927_ROMCE2	0x14000000	/* 16M */
#define JMR3927_ROMCE3	0x10000000	/* 64M */
#define JMR3927_ROMCE5	0x1d000000	/* 4M */
#define JMR3927_SDCS0	0x00000000	/* 32M */
#define JMR3927_SDCS1	0x02000000	/* 32M */
/* PCI Direct Mappings */

#define JMR3927_PCIMEM	0x08000000
#define JMR3927_PCIMEM_SIZE	0x08000000	/* 128M */
#define JMR3927_PCIIO	0x15000000
#define JMR3927_PCIIO_SIZE	0x01000000	/* 16M */

#define JMR3927_SDRAM_SIZE	0x02000000	/* 32M */
#define JMR3927_PORT_BASE	KSEG1

/* Address map (virtual address) */
#define JMR3927_ROM0_BASE	(KSEG1 + JMR3927_ROMCE0)
#define JMR3927_ROM1_BASE	(KSEG1 + JMR3927_ROMCE1)
#define JMR3927_IOC_BASE	(KSEG1 + JMR3927_ROMCE2)
#define JMR3927_PCIMEM_BASE	(KSEG1 + JMR3927_PCIMEM)
#define JMR3927_PCIIO_BASE	(KSEG1 + JMR3927_PCIIO)

#define JMR3927_IOC_REV_ADDR	(JMR3927_IOC_BASE + 0x00000000)
#define JMR3927_IOC_NVRAMB_ADDR	(JMR3927_IOC_BASE + 0x00010000)
#define JMR3927_IOC_LED_ADDR	(JMR3927_IOC_BASE + 0x00020000)
#define JMR3927_IOC_DIPSW_ADDR	(JMR3927_IOC_BASE + 0x00030000)
#define JMR3927_IOC_BREV_ADDR	(JMR3927_IOC_BASE + 0x00040000)
#define JMR3927_IOC_DTR_ADDR	(JMR3927_IOC_BASE + 0x00050000)
#define JMR3927_IOC_INTS1_ADDR	(JMR3927_IOC_BASE + 0x00080000)
#define JMR3927_IOC_INTS2_ADDR	(JMR3927_IOC_BASE + 0x00090000)
#define JMR3927_IOC_INTM_ADDR	(JMR3927_IOC_BASE + 0x000a0000)
#define JMR3927_IOC_INTP_ADDR	(JMR3927_IOC_BASE + 0x000b0000)
#define JMR3927_IOC_RESET_ADDR	(JMR3927_IOC_BASE + 0x000f0000)

/* Flash ROM */
#define JMR3927_FLASH_BASE	(JMR3927_ROM0_BASE)
#define JMR3927_FLASH_SIZE	0x00400000

/* bits for IOC_REV/IOC_BREV (high byte) */
#define JMR3927_IDT_MASK	0xfc
#define JMR3927_REV_MASK	0x03
#define JMR3927_IOC_IDT		0xe0

/* bits for IOC_INTS1/IOC_INTS2/IOC_INTM/IOC_INTP (high byte) */
#define JMR3927_IOC_INTB_PCIA	0
#define JMR3927_IOC_INTB_PCIB	1
#define JMR3927_IOC_INTB_PCIC	2
#define JMR3927_IOC_INTB_PCID	3
#define JMR3927_IOC_INTB_MODEM	4
#define JMR3927_IOC_INTB_INT6	5
#define JMR3927_IOC_INTB_INT7	6
#define JMR3927_IOC_INTB_SOFT	7
#define JMR3927_IOC_INTF_PCIA	(1 << JMR3927_IOC_INTF_PCIA)
#define JMR3927_IOC_INTF_PCIB	(1 << JMR3927_IOC_INTB_PCIB)
#define JMR3927_IOC_INTF_PCIC	(1 << JMR3927_IOC_INTB_PCIC)
#define JMR3927_IOC_INTF_PCID	(1 << JMR3927_IOC_INTB_PCID)
#define JMR3927_IOC_INTF_MODEM	(1 << JMR3927_IOC_INTB_MODEM)
#define JMR3927_IOC_INTF_INT6	(1 << JMR3927_IOC_INTB_INT6)
#define JMR3927_IOC_INTF_INT7	(1 << JMR3927_IOC_INTB_INT7)
#define JMR3927_IOC_INTF_SOFT	(1 << JMR3927_IOC_INTB_SOFT)

/* bits for IOC_RESET (high byte) */
#define JMR3927_IOC_RESET_CPU	1
#define JMR3927_IOC_RESET_PCI	2

#if defined(__BIG_ENDIAN)
#define jmr3927_ioc_reg_out(d, a)	((*(volatile unsigned char *)(a)) = (d))
#define jmr3927_ioc_reg_in(a)		(*(volatile unsigned char *)(a))
#elif defined(__LITTLE_ENDIAN)
#define jmr3927_ioc_reg_out(d, a)	((*(volatile unsigned char *)((a)^1)) = (d))
#define jmr3927_ioc_reg_in(a)		(*(volatile unsigned char *)((a)^1))
#else
#error "No Endian"
#endif

/* LED macro */
#define jmr3927_led_set(n/*0-16*/)	jmr3927_ioc_reg_out(~(n), JMR3927_IOC_LED_ADDR)

#define jmr3927_led_and_set(n/*0-16*/)	jmr3927_ioc_reg_out((~(n)) & jmr3927_ioc_reg_in(JMR3927_IOC_LED_ADDR), JMR3927_IOC_LED_ADDR)

/* DIPSW4 macro */
#define jmr3927_dipsw1()	(gpio_get_value(11) == 0)
#define jmr3927_dipsw2()	(gpio_get_value(10) == 0)
#define jmr3927_dipsw3()	((jmr3927_ioc_reg_in(JMR3927_IOC_DIPSW_ADDR) & 2) == 0)
#define jmr3927_dipsw4()	((jmr3927_ioc_reg_in(JMR3927_IOC_DIPSW_ADDR) & 1) == 0)

/*
 * IRQ mappings
 */

/* These are the virtual IRQ numbers, we divide all IRQ's into
 * 'spaces', the 'space' determines where and how to enable/disable
 * that particular IRQ on an JMR machine.  Add new 'spaces' as new
 * IRQ hardware is supported.
 */
#define JMR3927_NR_IRQ_IRC	16	/* On-Chip IRC */
#define JMR3927_NR_IRQ_IOC	8	/* PCI/MODEM/INT[6:7] */

#define JMR3927_IRQ_IRC	TXX9_IRQ_BASE
#define JMR3927_IRQ_IOC	(JMR3927_IRQ_IRC + JMR3927_NR_IRQ_IRC)
#define JMR3927_IRQ_END	(JMR3927_IRQ_IOC + JMR3927_NR_IRQ_IOC)

#define JMR3927_IRQ_IRC_INT0	(JMR3927_IRQ_IRC + TX3927_IR_INT0)
#define JMR3927_IRQ_IRC_INT1	(JMR3927_IRQ_IRC + TX3927_IR_INT1)
#define JMR3927_IRQ_IRC_INT2	(JMR3927_IRQ_IRC + TX3927_IR_INT2)
#define JMR3927_IRQ_IRC_INT3	(JMR3927_IRQ_IRC + TX3927_IR_INT3)
#define JMR3927_IRQ_IRC_INT4	(JMR3927_IRQ_IRC + TX3927_IR_INT4)
#define JMR3927_IRQ_IRC_INT5	(JMR3927_IRQ_IRC + TX3927_IR_INT5)
#define JMR3927_IRQ_IRC_SIO0	(JMR3927_IRQ_IRC + TX3927_IR_SIO0)
#define JMR3927_IRQ_IRC_SIO1	(JMR3927_IRQ_IRC + TX3927_IR_SIO1)
#define JMR3927_IRQ_IRC_SIO(ch)	(JMR3927_IRQ_IRC + TX3927_IR_SIO(ch))
#define JMR3927_IRQ_IRC_DMA	(JMR3927_IRQ_IRC + TX3927_IR_DMA)
#define JMR3927_IRQ_IRC_PIO	(JMR3927_IRQ_IRC + TX3927_IR_PIO)
#define JMR3927_IRQ_IRC_PCI	(JMR3927_IRQ_IRC + TX3927_IR_PCI)
#define JMR3927_IRQ_IRC_TMR(ch)	(JMR3927_IRQ_IRC + TX3927_IR_TMR(ch))
#define JMR3927_IRQ_IOC_PCIA	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIA)
#define JMR3927_IRQ_IOC_PCIB	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIB)
#define JMR3927_IRQ_IOC_PCIC	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIC)
#define JMR3927_IRQ_IOC_PCID	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCID)
#define JMR3927_IRQ_IOC_MODEM	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_MODEM)
#define JMR3927_IRQ_IOC_INT6	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_INT6)
#define JMR3927_IRQ_IOC_INT7	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_INT7)
#define JMR3927_IRQ_IOC_SOFT	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_SOFT)

/* IOC (PCI, MODEM) */
#define JMR3927_IRQ_IOCINT	JMR3927_IRQ_IRC_INT1
/* TC35815 100M Ether (JMR-TX3912:JPW4:2-3 Short) */
#define JMR3927_IRQ_ETHER0	JMR3927_IRQ_IRC_INT3

/* Clocks */
#define JMR3927_CORECLK	132710400	/* 132.7MHz */

/*
 * TX3927 Pin Configuration:
 *
 *	PCFG bits		Avail			Dead
 *	SELSIO[1:0]:11		RXD[1:0], TXD[1:0]	PIO[6:3]
 *	SELSIOC[0]:1		CTS[0], RTS[0]		INT[5:4]
 *	SELSIOC[1]:0,SELDSF:0,	GSDAO[0],GPCST[3]	CTS[1], RTS[1],DSF,
 *	  GDBGE*					  PIO[2:1]
 *	SELDMA[2]:1		DMAREQ[2],DMAACK[2]	PIO[13:12]
 *	SELTMR[2:0]:000					TIMER[1:0]
 *	SELCS:0,SELDMA[1]:0	PIO[11;10]		SDCS_CE[7:6],
 *							  DMAREQ[1],DMAACK[1]
 *	SELDMA[0]:1		DMAREQ[0],DMAACK[0]	PIO[9:8]
 *	SELDMA[3]:1		DMAREQ[3],DMAACK[3]	PIO[15:14]
 *	SELDONE:1		DMADONE			PIO[7]
 *
 * Usable pins are:
 *	RXD[1;0],TXD[1:0],CTS[0],RTS[0],
 *	DMAREQ[0,2,3],DMAACK[0,2,3],DMADONE,PIO[0,10,11]
 *	INT[3:0]
 */

void jmr3927_prom_init(void);
void jmr3927_irq_setup(void);
struct pci_dev;
int jmr3927_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);

#endif /* __ASM_TXX9_JMR3927_H */
