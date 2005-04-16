/*
 *	Low-Level PCI Support for SH7751 targets
 *
 *  Dustin McIntire (dustin@sensoria.com) (c) 2001
 *  Paul Mundt (lethal@linux-sh.org) (c) 2003
 *	
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */

#ifndef _PCI_SH7751_H_
#define _PCI_SH7751_H_

#include <linux/pci.h>

/* set debug level 4=verbose...1=terse */
//#define DEBUG_PCI 3
#undef DEBUG_PCI

#ifdef DEBUG_PCI
#define PCIDBG(n, x...) { if(DEBUG_PCI>=n) printk(x); }
#else
#define PCIDBG(n, x...)
#endif

/* startup values */
#define PCI_PROBE_BIOS 1
#define PCI_PROBE_CONF1 2
#define PCI_PROBE_CONF2 4
#define PCI_NO_SORT 0x100
#define PCI_BIOS_SORT 0x200
#define PCI_NO_CHECKS 0x400
#define PCI_ASSIGN_ROMS 0x1000
#define PCI_BIOS_IRQ_SCAN 0x2000

/* Platform Specific Values */
#define SH7751_VENDOR_ID             0x1054
#define SH7751_DEVICE_ID             0x3505
#define SH7751R_DEVICE_ID            0x350e

/* SH7751 Specific Values */
#define SH7751_PCI_CONFIG_BASE	     0xFD000000  /* Config space base addr */
#define SH7751_PCI_CONFIG_SIZE       0x1000000   /* Config space size */
#define SH7751_PCI_MEMORY_BASE	     0xFD000000  /* Memory space base addr */
#define SH7751_PCI_MEM_SIZE          0x01000000  /* Size of Memory window */
#define SH7751_PCI_IO_BASE           0xFE240000  /* IO space base address */
#define SH7751_PCI_IO_SIZE           0x40000     /* Size of IO window */

#define SH7751_PCIREG_BASE           0xFE200000  /* PCI regs base address */
#define PCI_REG(n)                  (SH7751_PCIREG_BASE+ n)

#define SH7751_PCICONF0            0x0           /* PCI Config Reg 0 */
  #define SH7751_PCICONF0_DEVID      0xFFFF0000  /* Device ID */
  #define SH7751_PCICONF0_VNDID      0x0000FFFF  /* Vendor ID */
#define SH7751_PCICONF1            0x4           /* PCI Config Reg 1 */
  #define SH7751_PCICONF1_DPE        0x80000000  /* Data Parity Error */
  #define SH7751_PCICONF1_SSE        0x40000000  /* System Error Status */
  #define SH7751_PCICONF1_RMA        0x20000000  /* Master Abort */
  #define SH7751_PCICONF1_RTA        0x10000000  /* Target Abort Rx Status */
  #define SH7751_PCICONF1_STA        0x08000000  /* Target Abort Exec Status */
  #define SH7751_PCICONF1_DEV        0x06000000  /* Timing Status */
  #define SH7751_PCICONF1_DPD        0x01000000  /* Data Parity Status */
  #define SH7751_PCICONF1_FBBC       0x00800000  /* Back 2 Back Status */
  #define SH7751_PCICONF1_UDF        0x00400000  /* User Defined Status */
  #define SH7751_PCICONF1_66M        0x00200000  /* 66Mhz Operation Status */
  #define SH7751_PCICONF1_PM         0x00100000  /* Power Management Status */
  #define SH7751_PCICONF1_PBBE       0x00000200  /* Back 2 Back Control */
  #define SH7751_PCICONF1_SER        0x00000100  /* SERR Output Control */
  #define SH7751_PCICONF1_WCC        0x00000080  /* Wait Cycle Control */
  #define SH7751_PCICONF1_PER        0x00000040  /* Parity Error Response */
  #define SH7751_PCICONF1_VPS        0x00000020  /* VGA Pallet Snoop */
  #define SH7751_PCICONF1_MWIE       0x00000010  /* Memory Write+Invalidate */
  #define SH7751_PCICONF1_SPC        0x00000008  /* Special Cycle Control */
  #define SH7751_PCICONF1_BUM        0x00000004  /* Bus Master Control */
  #define SH7751_PCICONF1_MES        0x00000002  /* Memory Space Control */
  #define SH7751_PCICONF1_IOS        0x00000001  /* I/O Space Control */
#define SH7751_PCICONF2            0x8           /* PCI Config Reg 2 */
  #define SH7751_PCICONF2_BCC        0xFF000000  /* Base Class Code */
  #define SH7751_PCICONF2_SCC        0x00FF0000  /* Sub-Class Code */
  #define SH7751_PCICONF2_RLPI       0x0000FF00  /* Programming Interface */
  #define SH7751_PCICONF2_REV        0x000000FF  /* Revision ID */
#define SH7751_PCICONF3            0xC           /* PCI Config Reg 3 */ 
  #define SH7751_PCICONF3_BIST7      0x80000000  /* Bist Supported */
  #define SH7751_PCICONF3_BIST6      0x40000000  /* Bist Executing */
  #define SH7751_PCICONF3_BIST3_0    0x0F000000  /* Bist Passed */
  #define SH7751_PCICONF3_HD7        0x00800000  /* Single Funtion device */
  #define SH7751_PCICONF3_HD6_0      0x007F0000  /* Configuration Layout */
  #define SH7751_PCICONF3_LAT        0x0000FF00  /* Latency Timer */
  #define SH7751_PCICONF3_CLS        0x000000FF  /* Cache Line Size */
#define SH7751_PCICONF4            0x10          /* PCI Config Reg 4 */
  #define SH7751_PCICONF4_BASE       0xFFFFFFFC  /* I/O Space Base Addr */
  #define SH7751_PCICONF4_ASI        0x00000001  /* Address Space Type */
#define SH7751_PCICONF5            0x14          /* PCI Config Reg 5 */
  #define SH7751_PCICONF5_BASE       0xFFFFFFF0  /* Mem Space Base Addr */
  #define SH7751_PCICONF5_LAP        0x00000008  /* Prefetch Enabled */
  #define SH7751_PCICONF5_LAT        0x00000006  /* Local Memory type */
  #define SH7751_PCICONF5_ASI        0x00000001  /* Address Space Type */  
#define SH7751_PCICONF6            0x18          /* PCI Config Reg 6 */
  #define SH7751_PCICONF6_BASE       0xFFFFFFF0  /* Mem Space Base Addr */
  #define SH7751_PCICONF6_LAP        0x00000008  /* Prefetch Enabled */
  #define SH7751_PCICONF6_LAT        0x00000006  /* Local Memory type */
  #define SH7751_PCICONF6_ASI        0x00000001  /* Address Space Type */  
/* PCICONF7 - PCICONF10 are undefined */
#define SH7751_PCICONF11           0x2C          /* PCI Config Reg 11 */
  #define SH7751_PCICONF11_SSID      0xFFFF0000  /* Subsystem ID */
  #define SH7751_PCICONF11_SVID      0x0000FFFF  /* Subsystem Vendor ID */
/* PCICONF12 is undefined */
#define SH7751_PCICONF13           0x34          /* PCI Config Reg 13 */
  #define SH7751_PCICONF13_CPTR      0x000000FF  /* PM function pointer */
/* PCICONF14 is undefined */
#define SH7751_PCICONF15           0x3C          /* PCI Config Reg 15 */
  #define SH7751_PCICONF15_IPIN      0x000000FF  /* Interrupt Pin */
#define SH7751_PCICONF16           0x40          /* PCI Config Reg 16 */
  #define SH7751_PCICONF16_PMES      0xF8000000  /* PME Support */
  #define SH7751_PCICONF16_D2S       0x04000000  /* D2 Support */
  #define SH7751_PCICONF16_D1S       0x02000000  /* D1 Support */
  #define SH7751_PCICONF16_DSI       0x00200000  /* Bit Device Init. */
  #define SH7751_PCICONF16_PMCK      0x00080000  /* Clock for PME req. */
  #define SH7751_PCICONF16_VER       0x00070000  /* PM Version */
  #define SH7751_PCICONF16_NIP       0x0000FF00  /* Next Item Pointer */
  #define SH7751_PCICONF16_CID       0x000000FF  /* Capability Identifier */
#define SH7751_PCICONF17           0x44          /* PCI Config Reg 17 */
  #define SH7751_PCICONF17_DATA      0xFF000000  /* Data field for PM */
  #define SH7751_PCICONF17_PMES      0x00800000  /* PME Status */
  #define SH7751_PCICONF17_DSCL      0x00600000  /* Data Scaling Value */
  #define SH7751_PCICONF17_DSEL      0x001E0000  /* Data Select */
  #define SH7751_PCICONF17_PMEN      0x00010000  /* PME Enable */
  #define SH7751_PCICONF17_PWST      0x00000003  /* Power State */
/* SH7715 Internal PCI Registers */
#define SH7751_PCICR               0x100         /* PCI Control Register */
  #define SH7751_PCICR_PREFIX        0xA5000000  /* CR prefix for write */
  #define SH7751_PCICR_TRSB          0x00000200  /* Target Read Single */
  #define SH7751_PCICR_BSWP          0x00000100  /* Target Byte Swap */
  #define SH7751_PCICR_PLUP          0x00000080  /* Enable PCI Pullup */
  #define SH7751_PCICR_ARBM          0x00000040  /* PCI Arbitration Mode */
  #define SH7751_PCICR_MD            0x00000030  /* MD9 and MD10 status */
  #define SH7751_PCICR_SERR          0x00000008  /* SERR output assert */
  #define SH7751_PCICR_INTA          0x00000004  /* INTA output assert */
  #define SH7751_PCICR_PRST          0x00000002  /* PCI Reset Assert */
  #define SH7751_PCICR_CFIN          0x00000001  /* Central Fun. Init Done */
#define SH7751_PCILSR0             0x104         /* PCI Local Space Register0 */
#define SH7751_PCILSR1             0x108         /* PCI Local Space Register1 */
#define SH7751_PCILAR0             0x10C         /* PCI Local Address Register1 */
#define SH7751_PCILAR1             0x110         /* PCI Local Address Register1 */
#define SH7751_PCIINT              0x114         /* PCI Interrupt Register */
  #define SH7751_PCIINT_MLCK         0x00008000  /* Master Lock Error */
  #define SH7751_PCIINT_TABT         0x00004000  /* Target Abort Error */
  #define SH7751_PCIINT_TRET         0x00000200  /* Target Retry Error */
  #define SH7751_PCIINT_MFDE         0x00000100  /* Master Func. Disable Error */
  #define SH7751_PCIINT_PRTY         0x00000080  /* Address Parity Error */
  #define SH7751_PCIINT_SERR         0x00000040  /* SERR Detection Error */
  #define SH7751_PCIINT_TWDP         0x00000020  /* Tgt. Write Parity Error */
  #define SH7751_PCIINT_TRDP         0x00000010  /* Tgt. Read Parity Error Det. */
  #define SH7751_PCIINT_MTABT        0x00000008  /* Master-Tgt. Abort Error */
  #define SH7751_PCIINT_MMABT        0x00000004  /* Master-Master Abort Error */
  #define SH7751_PCIINT_MWPD         0x00000002  /* Master Write PERR Detect */
  #define SH7751_PCIINT_MRPD         0x00000002  /* Master Read PERR Detect */
#define SH7751_PCIINTM             0x118         /* PCI Interrupt Mask Register */
#define SH7751_PCIALR              0x11C         /* Error Address Register */
#define SH7751_PCICLR              0x120         /* Error Command/Data Register */
  #define SH7751_PCICLR_MPIO         0x80000000  /* Error Command/Data Register */
  #define SH7751_PCICLR_MDMA0        0x40000000  /* DMA0 Transfer Error */
  #define SH7751_PCICLR_MDMA1        0x20000000  /* DMA1 Transfer Error */
  #define SH7751_PCICLR_MDMA2        0x10000000  /* DMA2 Transfer Error */
  #define SH7751_PCICLR_MDMA3        0x08000000  /* DMA3 Transfer Error */
  #define SH7751_PCICLR_TGT          0x04000000  /* Target Transfer Error */
  #define SH7751_PCICLR_CMDL         0x0000000F  /* PCI Command at Error */
#define SH7751_PCIAINT             0x130         /* Arbiter Interrupt Register */
  #define SH7751_PCIAINT_MBKN        0x00002000  /* Master Broken Interrupt */
  #define SH7751_PCIAINT_TBTO        0x00001000  /* Target Bus Time Out */
  #define SH7751_PCIAINT_MBTO        0x00001000  /* Master Bus Time Out */
  #define SH7751_PCIAINT_TABT        0x00000008  /* Target Abort */
  #define SH7751_PCIAINT_MABT        0x00000004  /* Master Abort */
  #define SH7751_PCIAINT_RDPE        0x00000002  /* Read Data Parity Error */
  #define SH7751_PCIAINT_WDPE        0x00000002  /* Write Data Parity Error */
#define SH7751_PCIAINTM            0x134         /* Arbiter Int. Mask Register */
#define SH7751_PCIBMLR             0x138         /* Error Bus Master Register */
  #define SH7751_PCIBMLR_REQ4        0x00000010  /* REQ4 bus master at error */
  #define SH7751_PCIBMLR_REQ3        0x00000008  /* REQ3 bus master at error */
  #define SH7751_PCIBMLR_REQ2        0x00000004  /* REQ2 bus master at error */
  #define SH7751_PCIBMLR_REQ1        0x00000002  /* REQ1 bus master at error */
  #define SH7751_PCIBMLR_REQ0        0x00000001  /* REQ0 bus master at error */
#define SH7751_PCIDMABT            0x140         /* DMA Transfer Arb. Register */
  #define SH7751_PCIDMABT_RRBN       0x00000001  /* DMA Arbitor Round-Robin */
#define SH7751_PCIDPA0             0x180         /* DMA0 Transfer Addr. Register */
#define SH7751_PCIDLA0             0x184         /* DMA0 Local Addr. Register */
#define SH7751_PCIDTC0             0x188         /* DMA0 Transfer Cnt. Register */
#define SH7751_PCIDCR0             0x18C         /* DMA0 Control Register */
  #define SH7751_PCIDCR_ALGN         0x00000600  /* DMA Alignment Mode */
  #define SH7751_PCIDCR_MAST         0x00000100  /* DMA Termination Type */
  #define SH7751_PCIDCR_INTM         0x00000080  /* DMA Interrupt Done Mask*/
  #define SH7751_PCIDCR_INTS         0x00000040  /* DMA Interrupt Done Status */
  #define SH7751_PCIDCR_LHLD         0x00000020  /* Local Address Control */
  #define SH7751_PCIDCR_PHLD         0x00000010  /* PCI Address Control*/
  #define SH7751_PCIDCR_IOSEL        0x00000008  /* PCI Address Space Type */
  #define SH7751_PCIDCR_DIR          0x00000004  /* DMA Transfer Direction */
  #define SH7751_PCIDCR_STOP         0x00000002  /* Force DMA Stop */
  #define SH7751_PCIDCR_STRT         0x00000001  /* DMA Start */
#define SH7751_PCIDPA1             0x190         /* DMA1 Transfer Addr. Register */
#define SH7751_PCIDLA1             0x194         /* DMA1 Local Addr. Register */
#define SH7751_PCIDTC1             0x198         /* DMA1 Transfer Cnt. Register */
#define SH7751_PCIDCR1             0x19C         /* DMA1 Control Register */
#define SH7751_PCIDPA2             0x1A0         /* DMA2 Transfer Addr. Register */
#define SH7751_PCIDLA2             0x1A4         /* DMA2 Local Addr. Register */
#define SH7751_PCIDTC2             0x1A8         /* DMA2 Transfer Cnt. Register */
#define SH7751_PCIDCR2             0x1AC         /* DMA2 Control Register */
#define SH7751_PCIDPA3             0x1B0         /* DMA3 Transfer Addr. Register */
#define SH7751_PCIDLA3             0x1B4         /* DMA3 Local Addr. Register */
#define SH7751_PCIDTC3             0x1B8         /* DMA3 Transfer Cnt. Register */
#define SH7751_PCIDCR3             0x1BC         /* DMA3 Control Register */
#define SH7751_PCIPAR              0x1C0         /* PIO Address Register */
  #define SH7751_PCIPAR_CFGEN        0x80000000  /* Configuration Enable */
  #define SH7751_PCIPAR_BUSNO        0x00FF0000  /* Config. Bus Number */
  #define SH7751_PCIPAR_DEVNO        0x0000FF00  /* Config. Device Number */
  #define SH7751_PCIPAR_REGAD        0x000000FC  /* Register Address Number */
#define SH7751_PCIMBR              0x1C4         /* Memory Base Address Register */
  #define SH7751_PCIMBR_MASK         0xFF000000  /* Memory Space Mask */
  #define SH7751_PCIMBR_LOCK         0x00000001  /* Lock Memory Space */
#define SH7751_PCIIOBR             0x1C8         /* I/O Base Address Register */
  #define SH7751_PCIIOBR_MASK         0xFFFC0000 /* IO Space Mask */
  #define SH7751_PCIIOBR_LOCK         0x00000001 /* Lock IO Space */
#define SH7751_PCIPINT             0x1CC         /* Power Mgmnt Int. Register */
  #define SH7751_PCIPINT_D3           0x00000002 /* D3 Pwr Mgmt. Interrupt */
  #define SH7751_PCIPINT_D0           0x00000001 /* D0 Pwr Mgmt. Interrupt */  
#define SH7751_PCIPINTM            0x1D0         /* Power Mgmnt Mask Register */
#define SH7751_PCICLKR             0x1D4         /* Clock Ctrl. Register */
  #define SH7751_PCICLKR_PCSTP        0x00000002 /* PCI Clock Stop */
  #define SH7751_PCICLKR_BCSTP        0x00000002 /* BCLK Clock Stop */
/* For definitions of BCR, MCR see ... */
#define SH7751_PCIBCR1             0x1E0         /* Memory BCR1 Register */
#define SH7751_PCIBCR2             0x1E4         /* Memory BCR2 Register */
#define SH7751_PCIWCR1             0x1E8         /* Wait Control 1 Register */
#define SH7751_PCIWCR2             0x1EC         /* Wait Control 2 Register */
#define SH7751_PCIWCR3             0x1F0         /* Wait Control 3 Register */
#define SH7751_PCIMCR              0x1F4         /* Memory Control Register */
#define SH7751_PCIBCR3		   0x1f8	 /* Memory BCR3 Register */
#define SH7751_PCIPCTR             0x200         /* Port Control Register */
  #define SH7751_PCIPCTR_P2EN        0x000400000 /* Port 2 Enable */
  #define SH7751_PCIPCTR_P1EN        0x000200000 /* Port 1 Enable */
  #define SH7751_PCIPCTR_P0EN        0x000100000 /* Port 0 Enable */
  #define SH7751_PCIPCTR_P2UP        0x000000020 /* Port2 Pull Up Enable */
  #define SH7751_PCIPCTR_P2IO        0x000000010 /* Port2 Output Enable */
  #define SH7751_PCIPCTR_P1UP        0x000000008 /* Port1 Pull Up Enable */
  #define SH7751_PCIPCTR_P1IO        0x000000004 /* Port1 Output Enable */
  #define SH7751_PCIPCTR_P0UP        0x000000002 /* Port0 Pull Up Enable */
  #define SH7751_PCIPCTR_P0IO        0x000000001 /* Port0 Output Enable */
#define SH7751_PCIPDTR             0x204         /* Port Data Register */
  #define SH7751_PCIPDTR_PB5         0x000000020 /* Port 5 Enable */
  #define SH7751_PCIPDTR_PB4         0x000000010 /* Port 4 Enable */
  #define SH7751_PCIPDTR_PB3         0x000000008 /* Port 3 Enable */
  #define SH7751_PCIPDTR_PB2         0x000000004 /* Port 2 Enable */
  #define SH7751_PCIPDTR_PB1         0x000000002 /* Port 1 Enable */
  #define SH7751_PCIPDTR_PB0         0x000000001 /* Port 0 Enable */
#define SH7751_PCIPDR              0x220         /* Port IO Data Register */

/* Memory Control Registers */
#define SH7751_BCR1                0xFF800000    /* Memory BCR1 Register */
#define SH7751_BCR2                0xFF800004    /* Memory BCR2 Register */
#define SH7751_BCR3                0xFF800050    /* Memory BCR3 Register */
#define SH7751_BCR4                0xFE0A00F0    /* Memory BCR4 Register */
#define SH7751_WCR1                0xFF800008    /* Wait Control 1 Register */
#define SH7751_WCR2                0xFF80000C    /* Wait Control 2 Register */
#define SH7751_WCR3                0xFF800010    /* Wait Control 3 Register */
#define SH7751_MCR                 0xFF800014    /* Memory Control Register */

/* General Memory Config Addresses */
#define SH7751_CS0_BASE_ADDR       0x0
#define SH7751_MEM_REGION_SIZE     0x04000000
#define SH7751_CS1_BASE_ADDR       (SH7751_CS0_BASE_ADDR + SH7751_MEM_REGION_SIZE)
#define SH7751_CS2_BASE_ADDR       (SH7751_CS1_BASE_ADDR + SH7751_MEM_REGION_SIZE)
#define SH7751_CS3_BASE_ADDR       (SH7751_CS2_BASE_ADDR + SH7751_MEM_REGION_SIZE)
#define SH7751_CS4_BASE_ADDR       (SH7751_CS3_BASE_ADDR + SH7751_MEM_REGION_SIZE)
#define SH7751_CS5_BASE_ADDR       (SH7751_CS4_BASE_ADDR + SH7751_MEM_REGION_SIZE)
#define SH7751_CS6_BASE_ADDR       (SH7751_CS5_BASE_ADDR + SH7751_MEM_REGION_SIZE)

/* General PCI values */
#define SH7751_PCI_HOST_BRIDGE		0x6

/* Flags */
#define SH7751_PCIC_NO_RESET	0x0001

/* External functions defined per platform i.e. Big Sur, SE... (these could be routed 
 * through the machine vectors... */
extern int pcibios_init_platform(void);
extern int pcibios_map_platform_irq(u8 slot, u8 pin);

struct sh7751_pci_address_space {
	unsigned long base;
	unsigned long size;
};

struct sh7751_pci_address_map {
	struct sh7751_pci_address_space window0;
	struct sh7751_pci_address_space window1;
	unsigned long flags;
};

/* arch/sh/drivers/pci/pci-sh7751.c */
extern int sh7751_pcic_init(struct sh7751_pci_address_map *map);

#endif /* _PCI_SH7751_H_ */

