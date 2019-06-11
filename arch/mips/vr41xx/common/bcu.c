// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  bcu.c, Bus Control Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2002	MontaVista Software Inc.
 *    Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2003-2005  Yoichi Yuasa <yuasa@linux-mips.org>
 */
/*
 * Changes:
 *  MontaVista Software Inc. <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Yoichi Yuasa <yuasa@linux-mips.org>
 *  - Added support for NEC VR4133.
 */
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cpu-type.h>
#include <asm/cpu.h>
#include <asm/io.h>

#define CLKSPEEDREG_TYPE1	(void __iomem *)KSEG1ADDR(0x0b000014)
#define CLKSPEEDREG_TYPE2	(void __iomem *)KSEG1ADDR(0x0f000014)
 #define CLKSP(x)		((x) & 0x001f)
 #define CLKSP_VR4133(x)	((x) & 0x0007)

 #define DIV2B			0x8000
 #define DIV3B			0x4000
 #define DIV4B			0x2000

 #define DIVT(x)		(((x) & 0xf000) >> 12)
 #define DIVVT(x)		(((x) & 0x0f00) >> 8)

 #define TDIVMODE(x)		(2 << (((x) & 0x1000) >> 12))
 #define VTDIVMODE(x)		(((x) & 0x0700) >> 8)

static unsigned long vr41xx_vtclock;
static unsigned long vr41xx_tclock;

unsigned long vr41xx_get_vtclock_frequency(void)
{
	return vr41xx_vtclock;
}

EXPORT_SYMBOL_GPL(vr41xx_get_vtclock_frequency);

unsigned long vr41xx_get_tclock_frequency(void)
{
	return vr41xx_tclock;
}

EXPORT_SYMBOL_GPL(vr41xx_get_tclock_frequency);

static inline uint16_t read_clkspeed(void)
{
	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121: return readw(CLKSPEEDREG_TYPE1);
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133: return readw(CLKSPEEDREG_TYPE2);
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	return 0;
}

static inline unsigned long calculate_pclock(uint16_t clkspeed)
{
	unsigned long pclock = 0;

	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121:
		pclock = 18432000 * 64;
		pclock /= CLKSP(clkspeed);
		break;
	case CPU_VR4122:
		pclock = 18432000 * 98;
		pclock /= CLKSP(clkspeed);
		break;
	case CPU_VR4131:
		pclock = 18432000 * 108;
		pclock /= CLKSP(clkspeed);
		break;
	case CPU_VR4133:
		switch (CLKSP_VR4133(clkspeed)) {
		case 0:
			pclock = 133000000;
			break;
		case 1:
			pclock = 149000000;
			break;
		case 2:
			pclock = 165900000;
			break;
		case 3:
			pclock = 199100000;
			break;
		case 4:
			pclock = 265900000;
			break;
		default:
			printk(KERN_INFO "Unknown PClock speed for NEC VR4133\n");
			break;
		}
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	printk(KERN_INFO "PClock: %ldHz\n", pclock);

	return pclock;
}

static inline unsigned long calculate_vtclock(uint16_t clkspeed, unsigned long pclock)
{
	unsigned long vtclock = 0;

	switch (current_cpu_type()) {
	case CPU_VR4111:
		/* The NEC VR4111 doesn't have the VTClock. */
		break;
	case CPU_VR4121:
		vtclock = pclock;
		/* DIVVT == 9 Divide by 1.5 . VTClock = (PClock * 6) / 9 */
		if (DIVVT(clkspeed) == 9)
			vtclock = pclock * 6;
		/* DIVVT == 10 Divide by 2.5 . VTClock = (PClock * 4) / 10 */
		else if (DIVVT(clkspeed) == 10)
			vtclock = pclock * 4;
		vtclock /= DIVVT(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vtclock);
		break;
	case CPU_VR4122:
		if(VTDIVMODE(clkspeed) == 7)
			vtclock = pclock / 1;
		else if(VTDIVMODE(clkspeed) == 1)
			vtclock = pclock / 2;
		else
			vtclock = pclock / VTDIVMODE(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vtclock);
		break;
	case CPU_VR4131:
	case CPU_VR4133:
		vtclock = pclock / VTDIVMODE(clkspeed);
		printk(KERN_INFO "VTClock: %ldHz\n", vtclock);
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	return vtclock;
}

static inline unsigned long calculate_tclock(uint16_t clkspeed, unsigned long pclock,
					     unsigned long vtclock)
{
	unsigned long tclock = 0;

	switch (current_cpu_type()) {
	case CPU_VR4111:
		if (!(clkspeed & DIV2B))
			tclock = pclock / 2;
		else if (!(clkspeed & DIV3B))
			tclock = pclock / 3;
		else if (!(clkspeed & DIV4B))
			tclock = pclock / 4;
		break;
	case CPU_VR4121:
		tclock = pclock / DIVT(clkspeed);
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		tclock = vtclock / TDIVMODE(clkspeed);
		break;
	default:
		printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
		break;
	}

	printk(KERN_INFO "TClock: %ldHz\n", tclock);

	return tclock;
}

void vr41xx_calculate_clock_frequency(void)
{
	unsigned long pclock;
	uint16_t clkspeed;

	clkspeed = read_clkspeed();

	pclock = calculate_pclock(clkspeed);
	vr41xx_vtclock = calculate_vtclock(clkspeed, pclock);
	vr41xx_tclock = calculate_tclock(clkspeed, pclock, vr41xx_vtclock);
}

EXPORT_SYMBOL_GPL(vr41xx_calculate_clock_frequency);
