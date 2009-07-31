/*
 * ca91c042.h
 *
 * Support for the Tundra Universe 1 and Universe II VME bridge chips
 *
 * Author: Tom Armistead
 * Updated and maintained by Ajit Prem
 * Copyright 2004 Motorola Inc.
 *
 * Derived from ca91c042.h by Michael Wyrick
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _ca91c042_h
#define _ca91c042_h

#ifndef	PCI_VENDOR_ID_TUNDRA
#define	PCI_VENDOR_ID_TUNDRA 0x10e3
#endif

#ifndef	PCI_DEVICE_ID_TUNDRA_CA91C042
#define	PCI_DEVICE_ID_TUNDRA_CA91C042 0x0000
#endif

//-----------------------------------------------------------------------------
// Public Functions
//-----------------------------------------------------------------------------
// This is the typedef for a VmeIrqHandler
typedef void (*TirqHandler) (int vmeirq, int vector, void *dev_id,
			     struct pt_regs * regs);
// This is the typedef for a DMA Transfer Callback function
typedef void (*TDMAcallback) (int status);

//  Returns the PCI baseaddress of the Universe chip
char *Universe_BaseAddr(void);
//  Returns the PCI IRQ That the universe is using
int Universe_IRQ(void);

char *mapvme(unsigned int pci, unsigned int vme, unsigned int size,
	     int image, int ctl);
void unmapvme(char *ptr, int image);

// Interrupt Stuff
void enable_vmeirq(unsigned int irq);
void disable_vmeirq(unsigned int irq);
int request_vmeirq(unsigned int irq, TirqHandler);
void free_vmeirq(unsigned int irq);

// DMA Stuff

int VME_Bus_Error(void);
int uni_procinfo(char *);

#define IRQ_VOWN    0x0001
#define IRQ_VIRQ1   0x0002
#define IRQ_VIRQ2   0x0004
#define IRQ_VIRQ3   0x0008
#define IRQ_VIRQ4   0x0010
#define IRQ_VIRQ5   0x0020
#define IRQ_VIRQ6   0x0040
#define IRQ_VIRQ7   0x0080
#define IRQ_DMA     0x0100
#define IRQ_LERR    0x0200
#define IRQ_VERR    0x0400
#define IRQ_res     0x0800
#define IRQ_IACK    0x1000
#define IRQ_SWINT   0x2000
#define IRQ_SYSFAIL 0x4000
#define IRQ_ACFAIL  0x8000

// See Page 2-77 in the Universe User Manual
typedef struct {
	unsigned int dctl;	// DMA Control
	unsigned int dtbc;	// Transfer Byte Count
	unsigned int dlv;	// PCI Address
	unsigned int res1;	// Reserved
	unsigned int dva;	// Vme Address
	unsigned int res2;	// Reserved
	unsigned int dcpp;	// Pointer to Numed Cmd Packet with rPN
	unsigned int res3;	// Reserved
} TDMA_Cmd_Packet;

/*
 * Below here is normaly not used by a user module
 */
#define  DMATIMEOUT 2*HZ;

// Define for the Universe
#define SEEK_SET 0
#define SEEK_CUR 1

#define CONFIG_REG_SPACE        0xA0000000

/* Universe Register Offsets */
/* general PCI configuration registers */
#define UNIV_PCI_ID             0x000
#define UNIV_PCI_CSR            0x004
#define UNIV_PCI_CLASS          0x008
#define UNIV_BM_PCI_CLASS_BASE          0xFF000000
#define UNIV_OF_PCI_CLASS_BASE          24
#define UNIV_BM_PCI_CLASS_SUB           0x00FF0000
#define UNIV_OF_PCI_CLASS_SUB           16
#define UNIV_BM_PCI_CLASS_PROG          0x0000FF00
#define UNIV_OF_PCI_CLASS_PROG          8
#define UNIV_BM_PCI_CLASS_RID           0x000000FF
#define UNIV_OF_PCI_CLASS_RID           0

#define UNIV_OF_PCI_CLASS_RID_UNIVERSE_I 0
#define UNIV_OF_PCI_CLASS_RID_UNIVERSE_II 1

#define UNIV_PCI_MISC0          0x00C
#define UNIV_BM_PCI_MISC0_BISTC         0x80000000
#define UNIV_BM_PCI_MISC0_SBIST         0x60000000
#define UNIV_BM_PCI_MISC0_CCODE         0x0F000000
#define UNIV_BM_PCI_MISC0_MFUNCT        0x00800000
#define UNIV_BM_PCI_MISC0_LAYOUT        0x007F0000
#define UNIV_BM_PCI_MISC0_LTIMER        0x0000FF00
#define UNIV_OF_PCI_MISC0_LTIMER        8
#define UNIV_PCI_BS             0x010
#define UNIV_PCI_MISC1          0x03C

#define UNIV_BM_LSI_CTL_EN      0x80000000
#define UNIV_BM_LSI_CTL_PWEN    0x40000000
#define UNIV_BM_LSI_CTL_VDW     0x00C00000
#define UNIV_OF_LSI_CTL_VDW     22
#define UNIV_BM_LSI_CTL_VAS     0x00070000
#define UNIV_OF_LSI_CTL_VAS     16
#define UNIV_BM_LSI_CTL_PGM     0x0000C000
#define UNIV_OF_LSI_CTL_PGM     14
#define UNIV_BM_LSI_CTL_SUPER   0x00003000
#define UNIV_OF_LSI_CTL_SUPER   12
#define UNIV_BM_LSI_CTL_VCT     0x00000100
#define UNIV_BM_LSI_CTL_LAS     0x00000003
#define UNIV_OF_LSI_CTL_LAS     0
#define UNIV_BM_LSI_CTL_RESERVED (~ (UNIV_BM_LSI_CTL_EN | UNIV_BM_LSI_CTL_PWEN | UNIV_BM_LSI_CTL_VDW | UNIV_BM_LSI_CTL_VAS | UNIV_BM_LSI_CTL_PGM | UNIV_BM_LSI_CTL_SUPER | UNIV_BM_LSI_CTL_VCT | UNIV_BM_LSI_CTL_LAS))

#define PCI_SIZE_8	    0x0001
#define PCI_SIZE_16	    0x0002
#define PCI_SIZE_32	    0x0003

#define IOCTL_SET_CTL 	0xF001
#define IOCTL_SET_BS	0xF002
#define IOCTL_SET_BD	0xF003
#define IOCTL_SET_TO	0xF004
#define IOCTL_PCI_SIZE  0xF005
#define IOCTL_SET_MODE 	0xF006
#define IOCTL_SET_WINT  0xF007	// Wait for interrupt before read

#define LSI0_CTL	0x0100
#define LSI0_BS		0x0104
#define LSI0_BD		0x0108
#define LSI0_TO		0x010C

#define LSI1_CTL	      0x0114
#define LSI1_BS		      0x0118
#define LSI1_BD		      0x011C
#define LSI1_TO		      0x0120

#define LSI2_CTL	      0x0128
#define LSI2_BS		      0x012C
#define LSI2_BD		      0x0130
#define LSI2_TO		      0x0134

#define LSI3_CTL	      0x013C
#define LSI3_BS		      0x0140
#define LSI3_BD		      0x0144
#define LSI3_TO		      0x0148

#define LSI4_CTL	      0x01A0
#define LSI4_BS		      0x01A4
#define LSI4_BD		      0x01A8
#define LSI4_TO		      0x01AC

#define LSI5_CTL	      0x01B4
#define LSI5_BS		      0x01B8
#define LSI5_BD		      0x01BC
#define LSI5_TO		      0x01C0

#define LSI6_CTL	      0x01C8
#define LSI6_BS		      0x01CC
#define LSI6_BD		      0x01D0
#define LSI6_TO		      0x01D4

#define LSI7_CTL	      0x01DC
#define LSI7_BS		      0x01E0
#define LSI7_BD		      0x01E4
#define LSI7_TO		      0x01E8

#define SCYC_CTL		0x0170
#define SCYC_ADDR		0x0174
#define SCYC_EN			0x0178
#define SCYC_CMP		0x017C
#define SCYC_SWP		0x0180
#define LMISC			0x0184
#define UNIV_BM_LMISC_CRT               0xF0000000
#define UNIV_OF_LMISC_CRT               28
#define UNIV_BM_LMISC_CWT               0x0F000000
#define UNIV_OF_LMISC_CWT               24
#define SLSI		        0x0188
#define UNIV_BM_SLSI_EN                 0x80000000
#define UNIV_BM_SLSI_PWEN               0x40000000
#define UNIV_BM_SLSI_VDW                0x00F00000
#define UNIV_OF_SLSI_VDW                20
#define UNIV_BM_SLSI_PGM                0x0000F000
#define UNIV_OF_SLSI_PGM                12
#define UNIV_BM_SLSI_SUPER              0x00000F00
#define UNIV_OF_SLSI_SUPER              8
#define UNIV_BM_SLSI_BS                 0x000000F6
#define UNIV_OF_SLSI_BS                 2
#define UNIV_BM_SLSI_LAS                0x00000003
#define UNIV_OF_SLSI_LAS                0
#define UNIV_BM_SLSI_RESERVED           0x3F0F0000
#define L_CMDERR		0x018C
#define LAERR		        0x0190

#define DCTL		        0x0200
#define DTBC		        0x0204
#define DLA			0x0208
#define DVA			0x0210
#define DCPP		        0x0218
#define DGCS		        0x0220
#define D_LLUE			0x0224

#define LINT_EN		      0x0300
#define UNIV_BM_LINT_ACFAIL             0x00008000
#define UNIV_BM_LINT_SYSFAIL            0x00004000
#define UNIV_BM_LINT_SW_INT             0x00002000
#define UNIV_BM_LINT_SW_IACK            0x00001000
#define UNIV_BM_LINT_VERR               0x00000400
#define UNIV_BM_LINT_LERR               0x00000200
#define UNIV_BM_LINT_DMA                0x00000100
#define UNIV_BM_LINT_LM                 0x00F00000
#define UNIV_BM_LINT_MBOX               0x000F0000
#define UNIV_BM_LINT_VIRQ               0x000000FE
#define UNIV_BM_LINT_VIRQ7              0x00000080
#define UNIV_BM_LINT_VIRQ6              0x00000040
#define UNIV_BM_LINT_VIRQ5              0x00000020
#define UNIV_BM_LINT_VIRQ4              0x00000010
#define UNIV_BM_LINT_VIRQ3              0x00000008
#define UNIV_BM_LINT_VIRQ2              0x00000004
#define UNIV_BM_LINT_VIRQ1              0x00000002
#define UNIV_BM_LINT_VOWN               0x00000001
#define LINT_STAT	      0x0304
#define LINT_MAP0	      0x0308
#define LINT_MAP1	      0x030C
#define VINT_EN		      0x0310
#define VINT_STAT	      0x0314
#define VINT_MAP0	      0x0318
#define VINT_MAP1	      0x031C
#define STATID		      0x0320
#define V1_STATID	      0x0324
#define V2_STATID	      0x0328
#define V3_STATID	      0x032C
#define V4_STATID	      0x0330
#define V5_STATID	      0x0334
#define V6_STATID	      0x0338
#define V7_STATID	      0x033C
#define LINT_MAP2	      0x0340
#define VINT_MAP2	      0x0344

#define MBOX0	              0x0348
#define MBOX1	              0x034C
#define MBOX2		      0x0350
#define MBOX3		      0x0354
#define SEMA0		      0x0358
#define SEMA1		      0x035C

#define MAST_CTL	      0x0400
#define UNIV_BM_MAST_CTL_MAXRTRY        0xF0000000
#define UNIV_OF_MAST_CTL_MAXRTRY        28
#define UNIV_BM_MAST_CTL_PWON           0x0F000000
#define UNIV_OF_MAST_CTL_PWON           24
#define UNIV_BM_MAST_CTL_VRL            0x00C00000
#define UNIV_OF_MAST_CTL_VRL            22
#define UNIV_BM_MAST_CTL_VRM            0x00200000
#define UNIV_BM_MAST_CTL_VREL           0x00100000
#define UNIV_BM_MAST_CTL_VOWN           0x00080000
#define UNIV_BM_MAST_CTL_VOWN_ACK       0x00040000
#define UNIV_BM_MAST_CTL_PABS           0x00001000
#define UNIV_BM_MAST_CTL_BUS_NO         0x0000000F
#define UNIV_OF_MAST_CTL_BUS_NO         0

#define MISC_CTL	      0x0404
#define UNIV_BM_MISC_CTL_VBTO           0xF0000000
#define UNIV_OF_MISC_CTL_VBTO           28
#define UNIV_BM_MISC_CTL_VARB           0x04000000
#define UNIV_BM_MISC_CTL_VARBTO         0x03000000
#define UNIV_OF_MISC_CTL_VARBTO         24
#define UNIV_BM_MISC_CTL_SW_LRST        0x00800000
#define UNIV_BM_MISC_CTL_SW_SRST        0x00400000
#define UNIV_BM_MISC_CTL_BI             0x00100000
#define UNIV_BM_MISC_CTL_ENGBI          0x00080000
#define UNIV_BM_MISC_CTL_RESCIND        0x00040000
#define UNIV_BM_MISC_CTL_SYSCON         0x00020000
#define UNIV_BM_MISC_CTL_V64AUTO        0x00010000
#define UNIV_BM_MISC_CTL_RESERVED       0x0820FFFF

#define MISC_STAT	      0x0408
#define UNIV_BM_MISC_STAT_ENDIAN        0x80000000
#define UNIV_BM_MISC_STAT_LCLSIZE       0x40000000
#define UNIV_BM_MISC_STAT_DY4AUTO       0x08000000
#define UNIV_BM_MISC_STAT_MYBBSY        0x00200000
#define UNIV_BM_MISC_STAT_DY4DONE       0x00080000
#define UNIV_BM_MISC_STAT_TXFE          0x00040000
#define UNIV_BM_MISC_STAT_RXFE          0x00020000
#define UNIV_BM_MISC_STAT_DY4AUTOID     0x0000FF00
#define UNIV_OF_MISC_STAT_DY4AUTOID     8

#define USER_AM		      0x040C

#define VSI0_CTL	      0x0F00
#define VSI0_BS		      0x0F04
#define VSI0_BD		      0x0F08
#define VSI0_TO		      0x0F0C

#define VSI1_CTL	      0x0F14
#define VSI1_BS		      0x0F18
#define VSI1_BD		      0x0F1C
#define VSI1_TO		      0x0F20

#define VSI2_CTL	      0x0F28
#define VSI2_BS		      0x0F2C
#define VSI2_BD		      0x0F30
#define VSI2_TO		      0x0F34

#define VSI3_CTL	      0x0F3C
#define VSI3_BS		      0x0F40
#define VSI3_BD		      0x0F44
#define VSI3_TO		      0x0F48

#define LM_CTL		      0x0F64
#define LM_BS		      0x0F68

#define VRAI_CTL	      0x0F70
#define UNIV_BM_VRAI_CTL_EN             0x80000000
#define UNIV_BM_VRAI_CTL_PGM            0x00C00000
#define UNIV_OF_VRAI_CTL_PGM            22
#define UNIV_BM_VRAI_CTL_SUPER          0x00300000
#define UNIV_OF_VRAI_CTL_SUPER          20
#define UNIV_BM_VRAI_CTL_VAS            0x00030000
#define UNIV_OF_VRAI_CTL_VAS            16

#define VRAI_BS		      0x0F74
#define VCSR_CTL	      0x0F80
#define VCSR_TO		      0x0F84
#define V_AMERR		      0x0F88
#define VAERR			0x0F8C

#define VSI4_CTL	      0x0F90
#define VSI4_BS		      0x0F94
#define VSI4_BD		      0x0F98
#define VSI4_TO		      0x0F9C

#define VSI5_CTL	      0x0FA4
#define VSI5_BS		      0x0FA8
#define VSI5_BD		      0x0FAC
#define VSI5_TO		      0x0FB0

#define VSI6_CTL	      0x0FB8
#define VSI6_BS		      0x0FBC
#define VSI6_BD		      0x0FC0
#define VSI6_TO		      0x0FC4

#define VSI7_CTL	      0x0FCC
#define VSI7_BS		      0x0FD0
#define VSI7_BD		      0x0FD4
#define VSI7_TO		      0x0FD8

#define VCSR_CLR	      0x0FF4
#define VCSR_SET	      0x0FF8
#define VCSR_BS		      0x0FFC

// DMA General Control/Status Register DGCS (0x220)
// 32-24 ||  GO   | STOPR | HALTR |   0   || CHAIN |   0   |   0   |   0   ||
// 23-16 ||              VON              ||             VOFF              ||
// 15-08 ||  ACT  | STOP  | HALT  |   0   || DONE  | LERR  | VERR  | P_ERR ||
// 07-00 ||   0   | INT_S | INT_H |   0   || I_DNE | I_LER | I_VER | I_PER ||

// VON - Length Per DMA VMEBus Transfer
//  0000 = None
//  0001 = 256 Bytes
//  0010 = 512
//  0011 = 1024
//  0100 = 2048
//  0101 = 4096
//  0110 = 8192
//  0111 = 16384

// VOFF - wait between DMA tenures
//  0000 = 0    us
//  0001 = 16
//  0010 = 32
//  0011 = 64
//  0100 = 128
//  0101 = 256
//  0110 = 512
//  0111 = 1024

#endif				/* _ca91c042_h */
