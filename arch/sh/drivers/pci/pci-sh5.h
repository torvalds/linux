/*
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Definitions for the SH5 PCI hardware.
 */
#ifndef __PCI_SH5_H
#define __PCI_SH5_H

/* Product ID */
#define PCISH5_PID		0x350d

/* vendor ID */
#define PCISH5_VID		0x1054

/* Configuration types */
#define ST_TYPE0                0x00    /* Configuration cycle type 0 */
#define ST_TYPE1                0x01    /* Configuration cycle type 1 */

/* VCR data */
#define PCISH5_VCR_STATUS      0x00
#define PCISH5_VCR_VERSION     0x08

/*
** ICR register offsets and bits
*/
#define PCISH5_ICR_CR          0x100   /* PCI control register values */
#define CR_PBAM                 (1<<12)
#define CR_PFCS                 (1<<11)
#define CR_FTO                  (1<<10)
#define CR_PFE                  (1<<9)
#define CR_TBS                  (1<<8)
#define CR_SPUE                 (1<<7)
#define CR_BMAM                 (1<<6)
#define CR_HOST                 (1<<5)
#define CR_CLKEN                (1<<4)
#define CR_SOCS                 (1<<3)
#define CR_IOCS                 (1<<2)
#define CR_RSTCTL               (1<<1)
#define CR_CFINT                (1<<0)
#define CR_LOCK_MASK            0xa5000000

#define PCISH5_ICR_INT         0x114   /* Interrupt registert values     */
#define INT_MADIM               (1<<2)

#define PCISH5_ICR_LSR0        0X104   /* Local space register values    */
#define PCISH5_ICR_LSR1        0X108   /* Local space register values    */
#define PCISH5_ICR_LAR0        0x10c   /* Local address register values  */
#define PCISH5_ICR_LAR1        0x110   /* Local address register values  */
#define PCISH5_ICR_INTM        0x118   /* Interrupt mask register values                         */
#define PCISH5_ICR_AIR         0x11c   /* Interrupt error address information register values    */
#define PCISH5_ICR_CIR         0x120   /* Interrupt error command information register values    */
#define PCISH5_ICR_AINT        0x130   /* Interrupt error arbiter interrupt register values      */
#define PCISH5_ICR_AINTM       0x134   /* Interrupt error arbiter interrupt mask register values */
#define PCISH5_ICR_BMIR        0x138   /* Interrupt error info register of bus master values     */
#define PCISH5_ICR_PAR         0x1c0   /* Pio address register values                            */
#define PCISH5_ICR_MBR         0x1c4   /* Memory space bank register values                      */
#define PCISH5_ICR_IOBR        0x1c8   /* I/O space bank register values                         */
#define PCISH5_ICR_PINT        0x1cc   /* power management interrupt register values             */
#define PCISH5_ICR_PINTM       0x1d0   /* power management interrupt mask register values        */
#define PCISH5_ICR_MBMR        0x1d8   /* memory space bank mask register values                 */
#define PCISH5_ICR_IOBMR       0x1dc   /* I/O space bank mask register values                    */
#define PCISH5_ICR_CSCR0       0x210   /* PCI cache snoop control register 0                     */
#define PCISH5_ICR_CSCR1       0x214   /* PCI cache snoop control register 1                     */
#define PCISH5_ICR_PDR         0x220   /* Pio data register values                               */

/* These are configs space registers */
#define PCISH5_ICR_CSR_VID     0x000	/* Vendor id                           */
#define PCISH5_ICR_CSR_DID     0x002   /* Device id                           */
#define PCISH5_ICR_CSR_CMD     0x004   /* Command register                    */
#define PCISH5_ICR_CSR_STATUS  0x006   /* Stautus                             */
#define PCISH5_ICR_CSR_IBAR0   0x010   /* I/O base address register           */
#define PCISH5_ICR_CSR_MBAR0   0x014   /* First  Memory base address register */
#define PCISH5_ICR_CSR_MBAR1   0x018   /* Second Memory base address register */

/* Base address of registers */
#define SH5PCI_ICR_BASE (PHYS_PCI_BLOCK + 0x00040000)
#define SH5PCI_IO_BASE  (PHYS_PCI_BLOCK + 0x00800000)
/* #define SH5PCI_VCR_BASE (P2SEG_PCICB_BLOCK + P2SEG)    */

extern unsigned long pcicr_virt;
/* Register selection macro */
#define PCISH5_ICR_REG(x)                ( pcicr_virt + (PCISH5_ICR_##x))
/* #define PCISH5_VCR_REG(x)                ( SH5PCI_VCR_BASE (PCISH5_VCR_##x)) */

/* Write I/O functions */
#define SH5PCI_WRITE(reg,val)        ctrl_outl((u32)(val),PCISH5_ICR_REG(reg))
#define SH5PCI_WRITE_SHORT(reg,val)  ctrl_outw((u16)(val),PCISH5_ICR_REG(reg))
#define SH5PCI_WRITE_BYTE(reg,val)   ctrl_outb((u8)(val),PCISH5_ICR_REG(reg))

/* Read I/O functions */
#define SH5PCI_READ(reg)             ctrl_inl(PCISH5_ICR_REG(reg))
#define SH5PCI_READ_SHORT(reg)       ctrl_inw(PCISH5_ICR_REG(reg))
#define SH5PCI_READ_BYTE(reg)        ctrl_inb(PCISH5_ICR_REG(reg))

/* Set PCI config bits */
#define SET_CONFIG_BITS(bus,devfn,where)  ((((bus) << 16) | ((devfn) << 8) | ((where) & ~3)) | 0x80000000)

/* Set PCI command register */
#define CONFIG_CMD(bus, devfn, where)            SET_CONFIG_BITS(bus->number,devfn,where)

/* Size converters */
#define PCISH5_MEM_SIZCONV(x)		  (((x / 0x40000) - 1) << 18)
#define PCISH5_IO_SIZCONV(x)		  (((x / 0x40000) - 1) << 18)

extern struct pci_ops sh5_pci_ops;

/* arch/sh/drivers/pci/pci-sh5.c */
int sh5_pci_init(struct pci_channel *chan);
int sh5pci_init(unsigned long memStart, unsigned long memSize);

#endif /* __PCI_SH5_H */
