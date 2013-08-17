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
  #define SH7751_PCICONF3_HD7        0x00800000  /* Single Function device */
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

#endif /* _PCI_SH7751_H_ */
