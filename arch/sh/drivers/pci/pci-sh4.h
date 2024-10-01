/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PCI_SH4_H
#define __PCI_SH4_H

#if defined(CONFIG_CPU_SUBTYPE_SH7780) || \
    defined(CONFIG_CPU_SUBTYPE_SH7785) || \
    defined(CONFIG_CPU_SUBTYPE_SH7763)
#include "pci-sh7780.h"
#else
#include "pci-sh7751.h"
#endif

#include <asm/io.h>

#define SH4_PCICR		0x100		/* PCI Control Register */
  #define SH4_PCICR_PREFIX	  0xA5000000	/* CR prefix for write */
  #define SH4_PCICR_FTO		  0x00000400	/* TRDY/IRDY Enable */
  #define SH4_PCICR_TRSB	  0x00000200	/* Target Read Single */
  #define SH4_PCICR_BSWP	  0x00000100	/* Target Byte Swap */
  #define SH4_PCICR_PLUP	  0x00000080	/* Enable PCI Pullup */
  #define SH4_PCICR_ARBM	  0x00000040	/* PCI Arbitration Mode */
  #define SH4_PCICR_MD		  0x00000030	/* MD9 and MD10 status */
  #define SH4_PCICR_SERR	  0x00000008	/* SERR output assert */
  #define SH4_PCICR_INTA	  0x00000004	/* INTA output assert */
  #define SH4_PCICR_PRST	  0x00000002	/* PCI Reset Assert */
  #define SH4_PCICR_CFIN	  0x00000001	/* Central Fun. Init Done */
#define SH4_PCILSR0		0x104		/* PCI Local Space Register0 */
#define SH4_PCILSR1		0x108		/* PCI Local Space Register1 */
#define SH4_PCILAR0		0x10C		/* PCI Local Addr Register1 */
#define SH4_PCILAR1		0x110		/* PCI Local Addr Register1 */
#define SH4_PCIINT		0x114		/* PCI Interrupt Register */
  #define SH4_PCIINT_MLCK	  0x00008000	/* Master Lock Error */
  #define SH4_PCIINT_TABT	  0x00004000	/* Target Abort Error */
  #define SH4_PCIINT_TRET	  0x00000200	/* Target Retry Error */
  #define SH4_PCIINT_MFDE	  0x00000100	/* Master Func. Disable Error */
  #define SH4_PCIINT_PRTY	  0x00000080	/* Address Parity Error */
  #define SH4_PCIINT_SERR	  0x00000040	/* SERR Detection Error */
  #define SH4_PCIINT_TWDP	  0x00000020	/* Tgt. Write Parity Error */
  #define SH4_PCIINT_TRDP	  0x00000010	/* Tgt. Read Parity Err Det. */
  #define SH4_PCIINT_MTABT	  0x00000008	/* Master-Tgt. Abort Error */
  #define SH4_PCIINT_MMABT	  0x00000004	/* Master-Master Abort Error */
  #define SH4_PCIINT_MWPD	  0x00000002	/* Master Write PERR Detect */
  #define SH4_PCIINT_MRPD	  0x00000001	/* Master Read PERR Detect */
#define SH4_PCIINTM		0x118		/* PCI Interrupt Mask */
  #define SH4_PCIINTM_TTADIM	  BIT(14)	/* Target-target abort interrupt */
  #define SH4_PCIINTM_TMTOIM	  BIT(9)	/* Target retry timeout */
  #define SH4_PCIINTM_MDEIM	  BIT(8)	/* Master function disable error */
  #define SH4_PCIINTM_APEDIM	  BIT(7)	/* Address parity error detection */
  #define SH4_PCIINTM_SDIM	  BIT(6)	/* SERR detection */
  #define SH4_PCIINTM_DPEITWM	  BIT(5)	/* Data parity error for target write */
  #define SH4_PCIINTM_PEDITRM	  BIT(4)	/* PERR detection for target read */
  #define SH4_PCIINTM_TADIMM	  BIT(3)	/* Target abort for master */
  #define SH4_PCIINTM_MADIMM	  BIT(2)	/* Master abort for master */
  #define SH4_PCIINTM_MWPDIM	  BIT(1)	/* Master write data parity error */
  #define SH4_PCIINTM_MRDPEIM	  BIT(0)	/* Master read data parity error */
#define SH4_PCIALR		0x11C		/* Error Address Register */
#define SH4_PCICLR		0x120		/* Error Command/Data */
  #define SH4_PCICLR_MPIO	  0x80000000
  #define SH4_PCICLR_MDMA0	  0x40000000	/* DMA0 Transfer Error */
  #define SH4_PCICLR_MDMA1	  0x20000000	/* DMA1 Transfer Error */
  #define SH4_PCICLR_MDMA2	  0x10000000	/* DMA2 Transfer Error */
  #define SH4_PCICLR_MDMA3	  0x08000000	/* DMA3 Transfer Error */
  #define SH4_PCICLR_TGT	  0x04000000	/* Target Transfer Error */
  #define SH4_PCICLR_CMDL	  0x0000000F	/* PCI Command at Error */
#define SH4_PCIAINT		0x130		/* Arbiter Interrupt Register */
  #define SH4_PCIAINT_MBKN	  0x00002000	/* Master Broken Interrupt */
  #define SH4_PCIAINT_TBTO	  0x00001000	/* Target Bus Time Out */
  #define SH4_PCIAINT_MBTO	  0x00000800	/* Master Bus Time Out */
  #define SH4_PCIAINT_TABT	  0x00000008	/* Target Abort */
  #define SH4_PCIAINT_MABT	  0x00000004	/* Master Abort */
  #define SH4_PCIAINT_RDPE	  0x00000002	/* Read Data Parity Error */
  #define SH4_PCIAINT_WDPE	  0x00000001	/* Write Data Parity Error */
#define SH4_PCIAINTM            0x134		/* Arbiter Int. Mask Register */
#define SH4_PCIBMLR		0x138		/* Error Bus Master Register */
  #define SH4_PCIBMLR_REQ4	  0x00000010	/* REQ4 bus master at error */
  #define SH4_PCIBMLR_REQ3	  0x00000008	/* REQ3 bus master at error */
  #define SH4_PCIBMLR_REQ2	  0x00000004	/* REQ2 bus master at error */
  #define SH4_PCIBMLR_REQ1	  0x00000002	/* REQ1 bus master at error */
  #define SH4_PCIBMLR_REQ0	  0x00000001	/* REQ0 bus master at error */
#define SH4_PCIDMABT		0x140		/* DMA Transfer Arb. Register */
  #define SH4_PCIDMABT_RRBN	  0x00000001	/* DMA Arbitor Round-Robin */
#define SH4_PCIDPA0		0x180		/* DMA0 Transfer Addr. */
#define SH4_PCIDLA0		0x184		/* DMA0 Local Addr. */
#define SH4_PCIDTC0		0x188		/* DMA0 Transfer Cnt. */
#define SH4_PCIDCR0		0x18C		/* DMA0 Control Register */
  #define SH4_PCIDCR_ALGN	  0x00000600	/* DMA Alignment Mode */
  #define SH4_PCIDCR_MAST	  0x00000100	/* DMA Termination Type */
  #define SH4_PCIDCR_INTM	  0x00000080	/* DMA Interrupt Done Mask*/
  #define SH4_PCIDCR_INTS	  0x00000040	/* DMA Interrupt Done Status */
  #define SH4_PCIDCR_LHLD	  0x00000020	/* Local Address Control */
  #define SH4_PCIDCR_PHLD	  0x00000010	/* PCI Address Control*/
  #define SH4_PCIDCR_IOSEL	  0x00000008	/* PCI Address Space Type */
  #define SH4_PCIDCR_DIR	  0x00000004	/* DMA Transfer Direction */
  #define SH4_PCIDCR_STOP	  0x00000002	/* Force DMA Stop */
  #define SH4_PCIDCR_STRT	  0x00000001	/* DMA Start */
#define SH4_PCIDPA1		0x190		/* DMA1 Transfer Addr. */
#define SH4_PCIDLA1		0x194		/* DMA1 Local Addr. */
#define SH4_PCIDTC1		0x198		/* DMA1 Transfer Cnt. */
#define SH4_PCIDCR1		0x19C		/* DMA1 Control Register */
#define SH4_PCIDPA2		0x1A0		/* DMA2 Transfer Addr. */
#define SH4_PCIDLA2		0x1A4		/* DMA2 Local Addr. */
#define SH4_PCIDTC2		0x1A8		/* DMA2 Transfer Cnt. */
#define SH4_PCIDCR2		0x1AC		/* DMA2 Control Register */
#define SH4_PCIDPA3		0x1B0		/* DMA3 Transfer Addr. */
#define SH4_PCIDLA3		0x1B4		/* DMA3 Local Addr. */
#define SH4_PCIDTC3		0x1B8		/* DMA3 Transfer Cnt. */
#define SH4_PCIDCR3		0x1BC		/* DMA3 Control Register */
#define SH4_PCIPAR		0x1C0		/* PIO Address Register */
  #define SH4_PCIPAR_CFGEN	  0x80000000	/* Configuration Enable */
  #define SH4_PCIPAR_BUSNO	  0x00FF0000	/* Config. Bus Number */
  #define SH4_PCIPAR_DEVNO	  0x0000FF00	/* Config. Device Number */
  #define SH4_PCIPAR_REGAD	  0x000000FC	/* Register Address Number */
#define SH4_PCIMBR		0x1C4		/* Memory Base Address */
  #define SH4_PCIMBR_MASK	  0xFF000000	/* Memory Space Mask */
  #define SH4_PCIMBR_LOCK	  0x00000001	/* Lock Memory Space */
#define SH4_PCIIOBR		0x1C8		/* I/O Base Address Register */
  #define SH4_PCIIOBR_MASK	  0xFFFC0000	/* IO Space Mask */
  #define SH4_PCIIOBR_LOCK	  0x00000001	/* Lock IO Space */
#define SH4_PCIPINT		0x1CC		/* Power Mgmnt Int. Register */
  #define SH4_PCIPINT_D3	  0x00000002	/* D3 Pwr Mgmt. Interrupt */
  #define SH4_PCIPINT_D0	  0x00000001	/* D0 Pwr Mgmt. Interrupt */
#define SH4_PCIPINTM		0x1D0		/* Power Mgmnt Mask Register */
#define SH4_PCICLKR		0x1D4		/* Clock Ctrl. Register */
  #define SH4_PCICLKR_PCSTP	  0x00000002	/* PCI Clock Stop */
  #define SH4_PCICLKR_BCSTP	  0x00000001	/* BCLK Clock Stop */
/* For definitions of BCR, MCR see ... */
#define SH4_PCIBCR1		0x1E0		/* Memory BCR1 Register */
  #define SH4_PCIMBR0		SH4_PCIBCR1
#define SH4_PCIBCR2		0x1E4		/* Memory BCR2 Register */
  #define SH4_PCIMBMR0		SH4_PCIBCR2
#define SH4_PCIWCR1		0x1E8		/* Wait Control 1 Register */
#define SH4_PCIWCR2		0x1EC		/* Wait Control 2 Register */
#define SH4_PCIWCR3		0x1F0		/* Wait Control 3 Register */
  #define SH4_PCIMBR2		SH4_PCIWCR3
#define SH4_PCIMCR		0x1F4		/* Memory Control Register */
#define SH4_PCIBCR3		0x1f8		/* Memory BCR3 Register */
#define SH4_PCIPCTR             0x200		/* Port Control Register */
  #define SH4_PCIPCTR_P2EN	  0x000400000	/* Port 2 Enable */
  #define SH4_PCIPCTR_P1EN	  0x000200000	/* Port 1 Enable */
  #define SH4_PCIPCTR_P0EN	  0x000100000	/* Port 0 Enable */
  #define SH4_PCIPCTR_P2UP	  0x000000020	/* Port2 Pull Up Enable */
  #define SH4_PCIPCTR_P2IO	  0x000000010	/* Port2 Output Enable */
  #define SH4_PCIPCTR_P1UP	  0x000000008	/* Port1 Pull Up Enable */
  #define SH4_PCIPCTR_P1IO	  0x000000004	/* Port1 Output Enable */
  #define SH4_PCIPCTR_P0UP	  0x000000002	/* Port0 Pull Up Enable */
  #define SH4_PCIPCTR_P0IO	  0x000000001	/* Port0 Output Enable */
#define SH4_PCIPDTR		0x204		/* Port Data Register */
  #define SH4_PCIPDTR_PB5	  0x000000020	/* Port 5 Enable */
  #define SH4_PCIPDTR_PB4	  0x000000010	/* Port 4 Enable */
  #define SH4_PCIPDTR_PB3	  0x000000008	/* Port 3 Enable */
  #define SH4_PCIPDTR_PB2	  0x000000004	/* Port 2 Enable */
  #define SH4_PCIPDTR_PB1	  0x000000002	/* Port 1 Enable */
  #define SH4_PCIPDTR_PB0	  0x000000001	/* Port 0 Enable */
#define SH4_PCIPDR		0x220		/* Port IO Data Register */

/* arch/sh/kernel/drivers/pci/ops-sh4.c */
extern struct pci_ops sh4_pci_ops;
int pci_fixup_pcic(struct pci_channel *chan);

struct sh4_pci_address_space {
	unsigned long base;
	unsigned long size;
};

struct sh4_pci_address_map {
	struct sh4_pci_address_space window0;
	struct sh4_pci_address_space window1;
};

static inline void pci_write_reg(struct pci_channel *chan,
				 unsigned long val, unsigned long reg)
{
	__raw_writel(val, chan->reg_base + reg);
}

static inline unsigned long pci_read_reg(struct pci_channel *chan,
					 unsigned long reg)
{
	return __raw_readl(chan->reg_base + reg);
}

#endif /* __PCI_SH4_H */
