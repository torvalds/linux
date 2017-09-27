/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * ip22.h: Definitions for SGI IP22 machines
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle
 */

#ifndef _SGI_IP22_H
#define _SGI_IP22_H

/*
 * These are the virtual IRQ numbers, we divide all IRQ's into
 * 'spaces', the 'space' determines where and how to enable/disable
 * that particular IRQ on an SGI machine. HPC DMA and MC DMA interrupts
 * are not supported this way. Driver is supposed to allocate HPC/MC
 * interrupt as shareable and then look to proper status bit (see
 * HAL2 driver). This will prevent many complications, trust me ;-)
 */

#include <irq.h>
#include <asm/sgi/ioc.h>

#define SGINT_EISA	0	/* 16 EISA irq levels (Indigo2) */
#define SGINT_CPU	MIPS_CPU_IRQ_BASE	/* MIPS CPU define 8 interrupt sources */
#define SGINT_LOCAL0	(SGINT_CPU+8)	/* 8 local0 irq levels */
#define SGINT_LOCAL1	(SGINT_CPU+16)	/* 8 local1 irq levels */
#define SGINT_LOCAL2	(SGINT_CPU+24)	/* 8 local2 vectored irq levels */
#define SGINT_LOCAL3	(SGINT_CPU+32)	/* 8 local3 vectored irq levels */
#define SGINT_END	(SGINT_CPU+40)	/* End of 'spaces' */

/*
 * Individual interrupt definitions for the Indy and Indigo2
 */

#define SGI_SOFT_0_IRQ	SGINT_CPU + 0
#define SGI_SOFT_1_IRQ	SGINT_CPU + 1
#define SGI_LOCAL_0_IRQ SGINT_CPU + 2
#define SGI_LOCAL_1_IRQ SGINT_CPU + 3
#define SGI_8254_0_IRQ	SGINT_CPU + 4
#define SGI_8254_1_IRQ	SGINT_CPU + 5
#define SGI_BUSERR_IRQ	SGINT_CPU + 6
#define SGI_TIMER_IRQ	SGINT_CPU + 7

#define SGI_FIFO_IRQ	SGINT_LOCAL0 + 0	/* FIFO full */
#define SGI_GIO_0_IRQ	SGI_FIFO_IRQ		/* GIO-0 */
#define SGI_WD93_0_IRQ	SGINT_LOCAL0 + 1	/* 1st onboard WD93 */
#define SGI_WD93_1_IRQ	SGINT_LOCAL0 + 2	/* 2nd onboard WD93 */
#define SGI_ENET_IRQ	SGINT_LOCAL0 + 3	/* onboard ethernet */
#define SGI_MCDMA_IRQ	SGINT_LOCAL0 + 4	/* MC DMA done */
#define SGI_PARPORT_IRQ SGINT_LOCAL0 + 5	/* Parallel port */
#define SGI_GIO_1_IRQ	SGINT_LOCAL0 + 6	/* GE / GIO-1 / 2nd-HPC */
#define SGI_MAP_0_IRQ	SGINT_LOCAL0 + 7	/* Mappable interrupt 0 */

#define SGI_GPL0_IRQ	SGINT_LOCAL1 + 0	/* General Purpose LOCAL1_N<0> */
#define SGI_PANEL_IRQ	SGINT_LOCAL1 + 1	/* front panel */
#define SGI_GPL2_IRQ	SGINT_LOCAL1 + 2	/* General Purpose LOCAL1_N<2> */
#define SGI_MAP_1_IRQ	SGINT_LOCAL1 + 3	/* Mappable interrupt 1 */
#define SGI_HPCDMA_IRQ	SGINT_LOCAL1 + 4	/* HPC DMA done */
#define SGI_ACFAIL_IRQ	SGINT_LOCAL1 + 5	/* AC fail */
#define SGI_VINO_IRQ	SGINT_LOCAL1 + 6	/* Indy VINO */
#define SGI_GIO_2_IRQ	SGINT_LOCAL1 + 7	/* Vert retrace / GIO-2 */

/* Mapped interrupts. These interrupts may be mapped to either 0, or 1 */
#define SGI_VERT_IRQ	SGINT_LOCAL2 + 0	/* INT3: newport vertical status */
#define SGI_EISA_IRQ	SGINT_LOCAL2 + 3	/* EISA interrupts */
#define SGI_KEYBD_IRQ	SGINT_LOCAL2 + 4	/* keyboard */
#define SGI_SERIAL_IRQ	SGINT_LOCAL2 + 5	/* onboard serial */
#define SGI_GIOEXP0_IRQ	(SGINT_LOCAL2 + 6)	/* Indy GIO EXP0 */
#define SGI_GIOEXP1_IRQ	(SGINT_LOCAL2 + 7)	/* Indy GIO EXP1 */

#define ip22_is_fullhouse()	(sgioc->sysid & SGIOC_SYSID_FULLHOUSE)

extern unsigned short ip22_eeprom_read(unsigned int *ctrl, int reg);
extern unsigned short ip22_nvram_read(int reg);

#endif
