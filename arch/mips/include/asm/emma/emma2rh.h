/*
 *  arch/mips/include/asm/emma/emma2rh.h
 *      This file is EMMA2RH common header.
 *
 *  Copyright (C) NEC Electronics Corporation 2005-2006
 *
 *  This file based on include/asm-mips/ddb5xxx/ddb5xxx.h
 *          Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_EMMA_EMMA2RH_H
#define __ASM_EMMA_EMMA2RH_H

#include <irq.h>

/*
 * EMMA2RH registers
 */
#define REGBASE 0x10000000

#define EMMA2RH_BHIF_STRAP_0	(0x000010+REGBASE)
#define EMMA2RH_BHIF_INT_ST_0	(0x000030+REGBASE)
#define EMMA2RH_BHIF_INT_ST_1	(0x000034+REGBASE)
#define EMMA2RH_BHIF_INT_ST_2	(0x000038+REGBASE)
#define EMMA2RH_BHIF_INT_EN_0	(0x000040+REGBASE)
#define EMMA2RH_BHIF_INT_EN_1	(0x000044+REGBASE)
#define EMMA2RH_BHIF_INT_EN_2	(0x000048+REGBASE)
#define EMMA2RH_BHIF_INT1_EN_0	(0x000050+REGBASE)
#define EMMA2RH_BHIF_INT1_EN_1	(0x000054+REGBASE)
#define EMMA2RH_BHIF_INT1_EN_2	(0x000058+REGBASE)
#define EMMA2RH_BHIF_SW_INT	(0x000070+REGBASE)
#define EMMA2RH_BHIF_SW_INT_EN	(0x000080+REGBASE)
#define EMMA2RH_BHIF_SW_INT_CLR	(0x000090+REGBASE)
#define EMMA2RH_BHIF_MAIN_CTRL	(0x0000b4+REGBASE)
#define EMMA2RH_BHIF_EXCEPT_VECT_BASE_ADDRESS	(0x0000c0+REGBASE)
#define EMMA2RH_GPIO_DIR	(0x110d20+REGBASE)
#define EMMA2RH_GPIO_INT_ST	(0x110d30+REGBASE)
#define EMMA2RH_GPIO_INT_MASK	(0x110d3c+REGBASE)
#define EMMA2RH_GPIO_INT_MODE	(0x110d48+REGBASE)
#define EMMA2RH_GPIO_INT_CND_A	(0x110d54+REGBASE)
#define EMMA2RH_GPIO_INT_CND_B	(0x110d60+REGBASE)
#define EMMA2RH_PBRD_INT_EN	(0x100010+REGBASE)
#define EMMA2RH_PBRD_CLKSEL	(0x100028+REGBASE)
#define EMMA2RH_PFUR0_BASE	(0x101000+REGBASE)
#define EMMA2RH_PFUR1_BASE	(0x102000+REGBASE)
#define EMMA2RH_PFUR2_BASE	(0x103000+REGBASE)
#define EMMA2RH_PIIC0_BASE	(0x107000+REGBASE)
#define EMMA2RH_PIIC1_BASE	(0x108000+REGBASE)
#define EMMA2RH_PIIC2_BASE	(0x109000+REGBASE)
#define EMMA2RH_PCI_CONTROL	(0x200000+REGBASE)
#define EMMA2RH_PCI_ARBIT_CTR	(0x200004+REGBASE)
#define EMMA2RH_PCI_IWIN0_CTR	(0x200010+REGBASE)
#define EMMA2RH_PCI_IWIN1_CTR	(0x200014+REGBASE)
#define EMMA2RH_PCI_INIT_ESWP	(0x200018+REGBASE)
#define EMMA2RH_PCI_INT		(0x200020+REGBASE)
#define EMMA2RH_PCI_INT_EN	(0x200024+REGBASE)
#define EMMA2RH_PCI_TWIN_CTR	(0x200030+REGBASE)
#define EMMA2RH_PCI_TWIN_BADR	(0x200034+REGBASE)
#define EMMA2RH_PCI_TWIN0_DADR	(0x200038+REGBASE)
#define EMMA2RH_PCI_TWIN1_DADR	(0x20003c+REGBASE)

/*
 *  Memory map (physical address)
 *
 *  Note most of the following address must be properly aligned by the
 *  corresponding size.  For example, if PCI_IO_SIZE is 16MB, then
 *  PCI_IO_BASE must be aligned along 16MB boundary.
 */

/* the actual ram size is detected at run-time */
#define EMMA2RH_RAM_BASE	0x00000000
#define EMMA2RH_RAM_SIZE	0x10000000	/* less than 256MB */

#define EMMA2RH_IO_BASE		0x10000000
#define EMMA2RH_IO_SIZE		0x01000000	/* 16 MB */

#define EMMA2RH_GENERALIO_BASE	0x11000000
#define EMMA2RH_GENERALIO_SIZE	0x01000000	/* 16 MB */

#define EMMA2RH_PCI_IO_BASE	0x12000000
#define EMMA2RH_PCI_IO_SIZE	0x02000000	/* 32 MB */

#define EMMA2RH_PCI_MEM_BASE	0x14000000
#define EMMA2RH_PCI_MEM_SIZE	0x08000000	/* 128 MB */

#define EMMA2RH_ROM_BASE	0x1c000000
#define EMMA2RH_ROM_SIZE	0x04000000	/* 64 MB */

#define EMMA2RH_PCI_CONFIG_BASE	EMMA2RH_PCI_IO_BASE
#define EMMA2RH_PCI_CONFIG_SIZE	EMMA2RH_PCI_IO_SIZE

#define NUM_CPU_IRQ		8
#define NUM_EMMA2RH_IRQ		96

#define CPU_EMMA2RH_CASCADE	2
#define CPU_IRQ_BASE		MIPS_CPU_IRQ_BASE
#define EMMA2RH_IRQ_BASE	(CPU_IRQ_BASE + NUM_CPU_IRQ)

/*
 * emma2rh irq defs
 */

#define EMMA2RH_IRQ_INT0	(0 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT1	(1 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT2	(2 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT3	(3 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT4	(4 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT5	(5 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT6	(6 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT7	(7 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT8	(8 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT9	(9 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT10	(10 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT11	(11 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT12	(12 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT13	(13 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT14	(14 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT15	(15 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT16	(16 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT17	(17 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT18	(18 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT19	(19 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT20	(20 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT21	(21 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT22	(22 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT23	(23 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT24	(24 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT25	(25 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT26	(26 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT27	(27 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT28	(28 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT29	(29 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT30	(30 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT31	(31 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT32	(32 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT33	(33 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT34	(34 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT35	(35 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT36	(36 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT37	(37 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT38	(38 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT39	(39 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT40	(40 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT41	(41 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT42	(42 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT43	(43 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT44	(44 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT45	(45 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT46	(46 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT47	(47 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT48	(48 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT49	(49 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT50	(50 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT51	(51 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT52	(52 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT53	(53 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT54	(54 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT55	(55 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT56	(56 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT57	(57 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT58	(58 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT59	(59 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT60	(60 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT61	(61 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT62	(62 + EMMA2RH_IRQ_BASE)
#define EMMA2RH_IRQ_INT63	(63 + EMMA2RH_IRQ_BASE)

#define EMMA2RH_IRQ_PFUR0	EMMA2RH_IRQ_INT49
#define EMMA2RH_IRQ_PFUR1	EMMA2RH_IRQ_INT50
#define EMMA2RH_IRQ_PFUR2	EMMA2RH_IRQ_INT51
#define EMMA2RH_IRQ_PIIC0	EMMA2RH_IRQ_INT56
#define EMMA2RH_IRQ_PIIC1	EMMA2RH_IRQ_INT57
#define EMMA2RH_IRQ_PIIC2	EMMA2RH_IRQ_INT58

/*
 *  EMMA2RH Register Access
 */

#define EMMA2RH_BASE (0xa0000000)

static inline void emma2rh_sync(void)
{
	volatile u32 *p = (volatile u32 *)0xbfc00000;
	(void)(*p);
}

static inline void emma2rh_out32(u32 offset, u32 val)
{
	*(volatile u32 *)(EMMA2RH_BASE | offset) = val;
	emma2rh_sync();
}

static inline u32 emma2rh_in32(u32 offset)
{
	u32 val = *(volatile u32 *)(EMMA2RH_BASE | offset);
	return val;
}

static inline void emma2rh_out16(u32 offset, u16 val)
{
	*(volatile u16 *)(EMMA2RH_BASE | offset) = val;
	emma2rh_sync();
}

static inline u16 emma2rh_in16(u32 offset)
{
	u16 val = *(volatile u16 *)(EMMA2RH_BASE | offset);
	return val;
}

static inline void emma2rh_out8(u32 offset, u8 val)
{
	*(volatile u8 *)(EMMA2RH_BASE | offset) = val;
	emma2rh_sync();
}

static inline u8 emma2rh_in8(u32 offset)
{
	u8 val = *(volatile u8 *)(EMMA2RH_BASE | offset);
	return val;
}

/**
 * IIC registers map
 **/

/*---------------------------------------------------------------------------*/
/* CNT - Control register (00H R/W)                                          */
/*---------------------------------------------------------------------------*/
#define SPT         0x00000001
#define STT         0x00000002
#define ACKE        0x00000004
#define WTIM        0x00000008
#define SPIE        0x00000010
#define WREL        0x00000020
#define LREL        0x00000040
#define IICE        0x00000080
#define CNT_RESERVED    0x000000ff	/* reserved bit 0 */

#define I2C_EMMA_START      (IICE | STT)
#define I2C_EMMA_STOP       (IICE | SPT)
#define I2C_EMMA_REPSTART   I2C_EMMA_START

/*---------------------------------------------------------------------------*/
/* STA - Status register (10H Read)                                          */
/*---------------------------------------------------------------------------*/
#define MSTS        0x00000080
#define ALD         0x00000040
#define EXC         0x00000020
#define COI         0x00000010
#define TRC         0x00000008
#define ACKD        0x00000004
#define STD         0x00000002
#define SPD         0x00000001

/*---------------------------------------------------------------------------*/
/* CSEL - Clock select register (20H R/W)                                    */
/*---------------------------------------------------------------------------*/
#define FCL         0x00000080
#define ND50        0x00000040
#define CLD         0x00000020
#define DAD         0x00000010
#define SMC         0x00000008
#define DFC         0x00000004
#define CL          0x00000003
#define CSEL_RESERVED   0x000000ff	/* reserved bit 0 */

#define FAST397     0x0000008b
#define FAST297     0x0000008a
#define FAST347     0x0000000b
#define FAST260     0x0000000a
#define FAST130     0x00000008
#define STANDARD108 0x00000083
#define STANDARD83  0x00000082
#define STANDARD95  0x00000003
#define STANDARD73  0x00000002
#define STANDARD36  0x00000001
#define STANDARD71  0x00000000

/*---------------------------------------------------------------------------*/
/* SVA - Slave address register (30H R/W)                                    */
/*---------------------------------------------------------------------------*/
#define SVA         0x000000fe

/*---------------------------------------------------------------------------*/
/* SHR - Shift register (40H R/W)                                            */
/*---------------------------------------------------------------------------*/
#define SR          0x000000ff

/*---------------------------------------------------------------------------*/
/* INT - Interrupt register (50H R/W)                                        */
/* INTM - Interrupt mask register (60H R/W)                                  */
/*---------------------------------------------------------------------------*/
#define INTE0       0x00000001

/***********************************************************************
 * I2C registers
 ***********************************************************************
 */
#define I2C_EMMA_CNT            0x00
#define I2C_EMMA_STA            0x10
#define I2C_EMMA_CSEL           0x20
#define I2C_EMMA_SVA            0x30
#define I2C_EMMA_SHR            0x40
#define I2C_EMMA_INT            0x50
#define I2C_EMMA_INTM           0x60

/*
 * include the board dependent part
 */
#ifdef CONFIG_NEC_MARKEINS
#include <asm/emma/markeins.h>
#else
#error "Unknown EMMA2RH board!"
#endif

#endif /* __ASM_EMMA_EMMA2RH_H */
