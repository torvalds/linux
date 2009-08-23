/*
 * include/asm-mips/vr41xx/irq.h
 *
 * Interrupt numbers for NEC VR4100 series.
 *
 * Copyright (C) 1999 Michael Klar
 * Copyright (C) 2001, 2002 Paul Mundt
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2002 TimeSys Corp.
 * Copyright (C) 2003-2006 Yoichi Yuasa <yuasa@linux-mips.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef __NEC_VR41XX_IRQ_H
#define __NEC_VR41XX_IRQ_H

/*
 * CPU core Interrupt Numbers
 */
#define MIPS_CPU_IRQ_BASE	0
#define MIPS_CPU_IRQ(x)		(MIPS_CPU_IRQ_BASE + (x))
#define MIPS_SOFTINT0_IRQ	MIPS_CPU_IRQ(0)
#define MIPS_SOFTINT1_IRQ	MIPS_CPU_IRQ(1)
#define INT0_IRQ		MIPS_CPU_IRQ(2)
#define INT1_IRQ		MIPS_CPU_IRQ(3)
#define INT2_IRQ		MIPS_CPU_IRQ(4)
#define INT3_IRQ		MIPS_CPU_IRQ(5)
#define INT4_IRQ		MIPS_CPU_IRQ(6)
#define TIMER_IRQ		MIPS_CPU_IRQ(7)

/*
 * SYINT1 Interrupt Numbers
 */
#define SYSINT1_IRQ_BASE	8
#define SYSINT1_IRQ(x)		(SYSINT1_IRQ_BASE + (x))
#define BATTRY_IRQ		SYSINT1_IRQ(0)
#define POWER_IRQ		SYSINT1_IRQ(1)
#define RTCLONG1_IRQ		SYSINT1_IRQ(2)
#define ELAPSEDTIME_IRQ		SYSINT1_IRQ(3)
/* RFU */
#define PIU_IRQ			SYSINT1_IRQ(5)
#define AIU_IRQ			SYSINT1_IRQ(6)
#define KIU_IRQ			SYSINT1_IRQ(7)
#define GIUINT_IRQ		SYSINT1_IRQ(8)
#define SIU_IRQ			SYSINT1_IRQ(9)
#define BUSERR_IRQ		SYSINT1_IRQ(10)
#define SOFTINT_IRQ		SYSINT1_IRQ(11)
#define CLKRUN_IRQ		SYSINT1_IRQ(12)
#define DOZEPIU_IRQ		SYSINT1_IRQ(13)
#define SYSINT1_IRQ_LAST	DOZEPIU_IRQ

/*
 * SYSINT2 Interrupt Numbers
 */
#define SYSINT2_IRQ_BASE	24
#define SYSINT2_IRQ(x)		(SYSINT2_IRQ_BASE + (x))
#define RTCLONG2_IRQ		SYSINT2_IRQ(0)
#define LED_IRQ			SYSINT2_IRQ(1)
#define HSP_IRQ			SYSINT2_IRQ(2)
#define TCLOCK_IRQ		SYSINT2_IRQ(3)
#define FIR_IRQ			SYSINT2_IRQ(4)
#define CEU_IRQ			SYSINT2_IRQ(4)	/* same number as FIR_IRQ */
#define DSIU_IRQ		SYSINT2_IRQ(5)
#define PCI_IRQ			SYSINT2_IRQ(6)
#define SCU_IRQ			SYSINT2_IRQ(7)
#define CSI_IRQ			SYSINT2_IRQ(8)
#define BCU_IRQ			SYSINT2_IRQ(9)
#define ETHERNET_IRQ		SYSINT2_IRQ(10)
#define SYSINT2_IRQ_LAST	ETHERNET_IRQ

/*
 * GIU Interrupt Numbers
 */
#define GIU_IRQ_BASE		40
#define GIU_IRQ(x)		(GIU_IRQ_BASE + (x))	/* IRQ 40-71 */
#define GIU_IRQ_LAST		GIU_IRQ(31)

/*
 * VRC4173 Interrupt Numbers
 */
#define VRC4173_IRQ_BASE	72
#define VRC4173_IRQ(x)		(VRC4173_IRQ_BASE + (x))
#define VRC4173_USB_IRQ		VRC4173_IRQ(0)
#define VRC4173_PCMCIA2_IRQ	VRC4173_IRQ(1)
#define VRC4173_PCMCIA1_IRQ	VRC4173_IRQ(2)
#define VRC4173_PS2CH2_IRQ	VRC4173_IRQ(3)
#define VRC4173_PS2CH1_IRQ	VRC4173_IRQ(4)
#define VRC4173_PIU_IRQ		VRC4173_IRQ(5)
#define VRC4173_AIU_IRQ		VRC4173_IRQ(6)
#define VRC4173_KIU_IRQ		VRC4173_IRQ(7)
#define VRC4173_GIU_IRQ		VRC4173_IRQ(8)
#define VRC4173_AC97_IRQ	VRC4173_IRQ(9)
#define VRC4173_AC97INT1_IRQ	VRC4173_IRQ(10)
/* RFU */
#define VRC4173_DOZEPIU_IRQ	VRC4173_IRQ(13)
#define VRC4173_IRQ_LAST	VRC4173_DOZEPIU_IRQ

#endif /* __NEC_VR41XX_IRQ_H */
