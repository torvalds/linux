/*
 * SNI specific definitions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 by Ralf Baechle
 * Copyright (C) 2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */
#ifndef __ASM_SNI_H
#define __ASM_SNI_H

extern unsigned int sni_brd_type;

#define SNI_BRD_10                 2
#define SNI_BRD_10NEW              3
#define SNI_BRD_TOWER_OASIC        4
#define SNI_BRD_MINITOWER          5
#define SNI_BRD_PCI_TOWER          6
#define SNI_BRD_RM200              7
#define SNI_BRD_PCI_MTOWER         8
#define SNI_BRD_PCI_DESKTOP        9
#define SNI_BRD_PCI_TOWER_CPLUS   10
#define SNI_BRD_PCI_MTOWER_CPLUS  11

/* RM400 cpu types */
#define SNI_CPU_M8021           0x01
#define SNI_CPU_M8030           0x04
#define SNI_CPU_M8031           0x06
#define SNI_CPU_M8034           0x0f
#define SNI_CPU_M8037           0x07
#define SNI_CPU_M8040           0x05
#define SNI_CPU_M8043           0x09
#define SNI_CPU_M8050           0x0b
#define SNI_CPU_M8053           0x0d

#define SNI_PORT_BASE		CKSEG1ADDR(0xb4000000)

#ifndef __MIPSEL__
/*
 * ASIC PCI registers for big endian configuration.
 */
#define PCIMT_UCONF		CKSEG1ADDR(0xbfff0004)
#define PCIMT_IOADTIMEOUT2	CKSEG1ADDR(0xbfff000c)
#define PCIMT_IOMEMCONF		CKSEG1ADDR(0xbfff0014)
#define PCIMT_IOMMU		CKSEG1ADDR(0xbfff001c)
#define PCIMT_IOADTIMEOUT1	CKSEG1ADDR(0xbfff0024)
#define PCIMT_DMAACCESS		CKSEG1ADDR(0xbfff002c)
#define PCIMT_DMAHIT		CKSEG1ADDR(0xbfff0034)
#define PCIMT_ERRSTATUS		CKSEG1ADDR(0xbfff003c)
#define PCIMT_ERRADDR		CKSEG1ADDR(0xbfff0044)
#define PCIMT_SYNDROME		CKSEG1ADDR(0xbfff004c)
#define PCIMT_ITPEND		CKSEG1ADDR(0xbfff0054)
#define  IT_INT2		0x01
#define  IT_INTD		0x02
#define  IT_INTC		0x04
#define  IT_INTB		0x08
#define  IT_INTA		0x10
#define  IT_EISA		0x20
#define  IT_SCSI		0x40
#define  IT_ETH			0x80
#define PCIMT_IRQSEL		CKSEG1ADDR(0xbfff005c)
#define PCIMT_TESTMEM		CKSEG1ADDR(0xbfff0064)
#define PCIMT_ECCREG		CKSEG1ADDR(0xbfff006c)
#define PCIMT_CONFIG_ADDRESS	CKSEG1ADDR(0xbfff0074)
#define PCIMT_ASIC_ID		CKSEG1ADDR(0xbfff007c)	/* read */
#define PCIMT_SOFT_RESET	CKSEG1ADDR(0xbfff007c)	/* write */
#define PCIMT_PIA_OE		CKSEG1ADDR(0xbfff0084)
#define PCIMT_PIA_DATAOUT	CKSEG1ADDR(0xbfff008c)
#define PCIMT_PIA_DATAIN	CKSEG1ADDR(0xbfff0094)
#define PCIMT_CACHECONF		CKSEG1ADDR(0xbfff009c)
#define PCIMT_INVSPACE		CKSEG1ADDR(0xbfff00a4)
#else
/*
 * ASIC PCI registers for little endian configuration.
 */
#define PCIMT_UCONF		CKSEG1ADDR(0xbfff0000)
#define PCIMT_IOADTIMEOUT2	CKSEG1ADDR(0xbfff0008)
#define PCIMT_IOMEMCONF		CKSEG1ADDR(0xbfff0010)
#define PCIMT_IOMMU		CKSEG1ADDR(0xbfff0018)
#define PCIMT_IOADTIMEOUT1	CKSEG1ADDR(0xbfff0020)
#define PCIMT_DMAACCESS		CKSEG1ADDR(0xbfff0028)
#define PCIMT_DMAHIT		CKSEG1ADDR(0xbfff0030)
#define PCIMT_ERRSTATUS		CKSEG1ADDR(0xbfff0038)
#define PCIMT_ERRADDR		CKSEG1ADDR(0xbfff0040)
#define PCIMT_SYNDROME		CKSEG1ADDR(0xbfff0048)
#define PCIMT_ITPEND		CKSEG1ADDR(0xbfff0050)
#define  IT_INT2		0x01
#define  IT_INTD		0x02
#define  IT_INTC		0x04
#define  IT_INTB		0x08
#define  IT_INTA		0x10
#define  IT_EISA		0x20
#define  IT_SCSI		0x40
#define  IT_ETH			0x80
#define PCIMT_IRQSEL		CKSEG1ADDR(0xbfff0058)
#define PCIMT_TESTMEM		CKSEG1ADDR(0xbfff0060)
#define PCIMT_ECCREG		CKSEG1ADDR(0xbfff0068)
#define PCIMT_CONFIG_ADDRESS	CKSEG1ADDR(0xbfff0070)
#define PCIMT_ASIC_ID		CKSEG1ADDR(0xbfff0078)	/* read */
#define PCIMT_SOFT_RESET	CKSEG1ADDR(0xbfff0078)	/* write */
#define PCIMT_PIA_OE		CKSEG1ADDR(0xbfff0080)
#define PCIMT_PIA_DATAOUT	CKSEG1ADDR(0xbfff0088)
#define PCIMT_PIA_DATAIN	CKSEG1ADDR(0xbfff0090)
#define PCIMT_CACHECONF		CKSEG1ADDR(0xbfff0098)
#define PCIMT_INVSPACE		CKSEG1ADDR(0xbfff00a0)
#endif

#define PCIMT_PCI_CONF		CKSEG1ADDR(0xbfff0100)

/*
 * Data port for the PCI bus in IO space
 */
#define PCIMT_CONFIG_DATA	0x0cfc

/*
 * Board specific registers
 */
#define PCIMT_CSMSR		CKSEG1ADDR(0xbfd00000)
#define PCIMT_CSSWITCH		CKSEG1ADDR(0xbfd10000)
#define PCIMT_CSITPEND		CKSEG1ADDR(0xbfd20000)
#define PCIMT_AUTO_PO_EN	CKSEG1ADDR(0xbfd30000)
#define PCIMT_CLR_TEMP		CKSEG1ADDR(0xbfd40000)
#define PCIMT_AUTO_PO_DIS	CKSEG1ADDR(0xbfd50000)
#define PCIMT_EXMSR		CKSEG1ADDR(0xbfd60000)
#define PCIMT_UNUSED1		CKSEG1ADDR(0xbfd70000)
#define PCIMT_CSWCSM		CKSEG1ADDR(0xbfd80000)
#define PCIMT_UNUSED2		CKSEG1ADDR(0xbfd90000)
#define PCIMT_CSLED		CKSEG1ADDR(0xbfda0000)
#define PCIMT_CSMAPISA		CKSEG1ADDR(0xbfdb0000)
#define PCIMT_CSRSTBP		CKSEG1ADDR(0xbfdc0000)
#define PCIMT_CLRPOFF		CKSEG1ADDR(0xbfdd0000)
#define PCIMT_CSTIMER		CKSEG1ADDR(0xbfde0000)
#define PCIMT_PWDN		CKSEG1ADDR(0xbfdf0000)

/*
 * A20R based boards
 */
#define A20R_PT_CLOCK_BASE      CKSEG1ADDR(0xbc040000)
#define A20R_PT_TIM0_ACK        CKSEG1ADDR(0xbc050000)
#define A20R_PT_TIM1_ACK        CKSEG1ADDR(0xbc060000)

#define SNI_A20R_IRQ_BASE       MIPS_CPU_IRQ_BASE
#define SNI_A20R_IRQ_TIMER      (SNI_A20R_IRQ_BASE+5)

#define SNI_PCIT_INT_REG        CKSEG1ADDR(0xbfff000c)

#define SNI_PCIT_INT_START      24
#define SNI_PCIT_INT_END        30

#define PCIT_IRQ_ETHERNET       (MIPS_CPU_IRQ_BASE + 5)
#define PCIT_IRQ_INTA           (SNI_PCIT_INT_START + 0)
#define PCIT_IRQ_INTB           (SNI_PCIT_INT_START + 1)
#define PCIT_IRQ_INTC           (SNI_PCIT_INT_START + 2)
#define PCIT_IRQ_INTD           (SNI_PCIT_INT_START + 3)
#define PCIT_IRQ_SCSI0          (SNI_PCIT_INT_START + 4)
#define PCIT_IRQ_SCSI1          (SNI_PCIT_INT_START + 5)


/*
 * Interrupt 0-16 are EISA interrupts.  Interrupts from 16 on are assigned
 * to the other interrupts generated by ASIC PCI.
 *
 * INT2 is a wired-or of the push button interrupt, high temperature interrupt
 * ASIC PCI interrupt.
 */
#define PCIMT_KEYBOARD_IRQ	 1
#define PCIMT_IRQ_INT2		24
#define PCIMT_IRQ_INTD		25
#define PCIMT_IRQ_INTC		26
#define PCIMT_IRQ_INTB		27
#define PCIMT_IRQ_INTA		28
#define PCIMT_IRQ_EISA		29
#define PCIMT_IRQ_SCSI		30

#define PCIMT_IRQ_ETHERNET	(MIPS_CPU_IRQ_BASE+6)

#if 0
#define PCIMT_IRQ_TEMPERATURE	24
#define PCIMT_IRQ_EISA_NMI	25
#define PCIMT_IRQ_POWER_OFF	26
#define PCIMT_IRQ_BUTTON	27
#endif

/*
 * Base address for the mapped 16mb EISA bus segment.
 */
#define PCIMT_EISA_BASE		CKSEG1ADDR(0xb0000000)

/* PCI EISA Interrupt acknowledge  */
#define PCIMT_INT_ACKNOWLEDGE	CKSEG1ADDR(0xba000000)

/*
 *  SNI ID PROM
 *
 * SNI_IDPROM_MEMSIZE  Memsize in 16MB quantities
 * SNI_IDPROM_BRDTYPE  Board Type
 * SNI_IDPROM_CPUTYPE  CPU Type on RM400
 */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define __SNI_END 0
#endif
#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define __SNI_END 3
#endif
#define SNI_IDPROM_BASE        CKSEG1ADDR(0x1ff00000)
#define SNI_IDPROM_MEMSIZE     (SNI_IDPROM_BASE + (0x28 ^ __SNI_END))
#define SNI_IDPROM_BRDTYPE     (SNI_IDPROM_BASE + (0x29 ^ __SNI_END))
#define SNI_IDPROM_CPUTYPE     (SNI_IDPROM_BASE + (0x30 ^ __SNI_END))

#define SNI_IDPROM_SIZE	0x1000

/* board specific init functions */
extern void sni_a20r_init(void);
extern void sni_pcit_init(void);
extern void sni_rm200_init(void);
extern void sni_pcimt_init(void);

/* board specific irq init functions */
extern void sni_a20r_irq_init(void);
extern void sni_pcit_irq_init(void);
extern void sni_pcit_cplus_irq_init(void);
extern void sni_rm200_irq_init(void);
extern void sni_pcimt_irq_init(void);

/* timer inits */
extern void sni_cpu_time_init(void);

/* eisa init for RM200/400 */
#ifdef CONFIG_EISA
extern int sni_eisa_root_init(void);
#else
static inline int sni_eisa_root_init(void)
{
	return 0;
}
#endif

/* common irq stuff */
extern void (*sni_hwint)(void);
extern struct irqaction sni_isa_irq;

#endif /* __ASM_SNI_H */
