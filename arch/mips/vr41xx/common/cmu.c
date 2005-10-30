/*
 *  cmu.c, Clock Mask Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2001-2002  MontaVista Software Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com or source@mvista.com>
 *  Copuright (C) 2003-2005  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
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
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  - Added support for NEC VR4133.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/vr41xx/vr41xx.h>

#define CMU_TYPE1_BASE	0x0b000060UL
#define CMU_TYPE1_SIZE	0x4

#define CMU_TYPE2_BASE	0x0f000060UL
#define CMU_TYPE2_SIZE	0x4

#define CMU_TYPE3_BASE	0x0f000060UL
#define CMU_TYPE3_SIZE	0x8

#define CMUCLKMSK	0x0
 #define MSKPIU		0x0001
 #define MSKSIU		0x0002
 #define MSKAIU		0x0004
 #define MSKKIU		0x0008
 #define MSKFIR		0x0010
 #define MSKDSIU	0x0820
 #define MSKCSI		0x0040
 #define MSKPCIU	0x0080
 #define MSKSSIU	0x0100
 #define MSKSHSP	0x0200
 #define MSKFFIR	0x0400
 #define MSKSCSI	0x1000
 #define MSKPPCIU	0x2000
#define CMUCLKMSK2	0x4
 #define MSKCEU		0x0001
 #define MSKMAC0	0x0002
 #define MSKMAC1	0x0004

static void __iomem *cmu_base;
static uint16_t cmuclkmsk, cmuclkmsk2;
static DEFINE_SPINLOCK(cmu_lock);

#define cmu_read(offset)		readw(cmu_base + (offset))
#define cmu_write(offset, value)	writew((value), cmu_base + (offset))

void vr41xx_supply_clock(vr41xx_clock_t clock)
{
	spin_lock_irq(&cmu_lock);

	switch (clock) {
	case PIU_CLOCK:
		cmuclkmsk |= MSKPIU;
		break;
	case SIU_CLOCK:
		cmuclkmsk |= MSKSIU | MSKSSIU;
		break;
	case AIU_CLOCK:
		cmuclkmsk |= MSKAIU;
		break;
	case KIU_CLOCK:
		cmuclkmsk |= MSKKIU;
		break;
	case FIR_CLOCK:
		cmuclkmsk |= MSKFIR | MSKFFIR;
		break;
	case DSIU_CLOCK:
		if (current_cpu_data.cputype == CPU_VR4111 ||
		    current_cpu_data.cputype == CPU_VR4121)
			cmuclkmsk |= MSKDSIU;
		else
			cmuclkmsk |= MSKSIU | MSKDSIU;
		break;
	case CSI_CLOCK:
		cmuclkmsk |= MSKCSI | MSKSCSI;
		break;
	case PCIU_CLOCK:
		cmuclkmsk |= MSKPCIU;
		break;
	case HSP_CLOCK:
		cmuclkmsk |= MSKSHSP;
		break;
	case PCI_CLOCK:
		cmuclkmsk |= MSKPPCIU;
		break;
	case CEU_CLOCK:
		cmuclkmsk2 |= MSKCEU;
		break;
	case ETHER0_CLOCK:
		cmuclkmsk2 |= MSKMAC0;
		break;
	case ETHER1_CLOCK:
		cmuclkmsk2 |= MSKMAC1;
		break;
	default:
		break;
	}

	if (clock == CEU_CLOCK || clock == ETHER0_CLOCK ||
	    clock == ETHER1_CLOCK)
		cmu_write(CMUCLKMSK2, cmuclkmsk2);
	else
		cmu_write(CMUCLKMSK, cmuclkmsk);

	spin_unlock_irq(&cmu_lock);
}

EXPORT_SYMBOL_GPL(vr41xx_supply_clock);

void vr41xx_mask_clock(vr41xx_clock_t clock)
{
	spin_lock_irq(&cmu_lock);

	switch (clock) {
	case PIU_CLOCK:
		cmuclkmsk &= ~MSKPIU;
		break;
	case SIU_CLOCK:
		if (current_cpu_data.cputype == CPU_VR4111 ||
		    current_cpu_data.cputype == CPU_VR4121) {
			cmuclkmsk &= ~(MSKSIU | MSKSSIU);
		} else {
			if (cmuclkmsk & MSKDSIU)
				cmuclkmsk &= ~MSKSSIU;
			else
				cmuclkmsk &= ~(MSKSIU | MSKSSIU);
		}
		break;
	case AIU_CLOCK:
		cmuclkmsk &= ~MSKAIU;
		break;
	case KIU_CLOCK:
		cmuclkmsk &= ~MSKKIU;
		break;
	case FIR_CLOCK:
		cmuclkmsk &= ~(MSKFIR | MSKFFIR);
		break;
	case DSIU_CLOCK:
		if (current_cpu_data.cputype == CPU_VR4111 ||
		    current_cpu_data.cputype == CPU_VR4121) {
			cmuclkmsk &= ~MSKDSIU;
		} else {
			if (cmuclkmsk & MSKSSIU)
				cmuclkmsk &= ~MSKDSIU;
			else
				cmuclkmsk &= ~(MSKSIU | MSKDSIU);
		}
		break;
	case CSI_CLOCK:
		cmuclkmsk &= ~(MSKCSI | MSKSCSI);
		break;
	case PCIU_CLOCK:
		cmuclkmsk &= ~MSKPCIU;
		break;
	case HSP_CLOCK:
		cmuclkmsk &= ~MSKSHSP;
		break;
	case PCI_CLOCK:
		cmuclkmsk &= ~MSKPPCIU;
		break;
	case CEU_CLOCK:
		cmuclkmsk2 &= ~MSKCEU;
		break;
	case ETHER0_CLOCK:
		cmuclkmsk2 &= ~MSKMAC0;
		break;
	case ETHER1_CLOCK:
		cmuclkmsk2 &= ~MSKMAC1;
		break;
	default:
		break;
	}

	if (clock == CEU_CLOCK || clock == ETHER0_CLOCK ||
	    clock == ETHER1_CLOCK)
		cmu_write(CMUCLKMSK2, cmuclkmsk2);
	else
		cmu_write(CMUCLKMSK, cmuclkmsk);

	spin_unlock_irq(&cmu_lock);
}

EXPORT_SYMBOL_GPL(vr41xx_mask_clock);

static int __init vr41xx_cmu_init(void)
{
	unsigned long start, size;

	switch (current_cpu_data.cputype) {
        case CPU_VR4111:
        case CPU_VR4121:
		start = CMU_TYPE1_BASE;
		size = CMU_TYPE1_SIZE;
                break;
        case CPU_VR4122:
        case CPU_VR4131:
		start = CMU_TYPE2_BASE;
		size = CMU_TYPE2_SIZE;
		break;
        case CPU_VR4133:
		start = CMU_TYPE3_BASE;
		size = CMU_TYPE3_SIZE;
                break;
	default:
		panic("Unexpected CPU of NEC VR4100 series");
		break;
        }

	if (request_mem_region(start, size, "CMU") == NULL)
		return -EBUSY;

	cmu_base = ioremap(start, size);
	if (cmu_base == NULL) {
		release_mem_region(start, size);
		return -EBUSY;
	}

	cmuclkmsk = cmu_read(CMUCLKMSK);
	if (current_cpu_data.cputype == CPU_VR4133)
		cmuclkmsk2 = cmu_read(CMUCLKMSK2);

	spin_lock_init(&cmu_lock);

	return 0;
}

core_initcall(vr41xx_cmu_init);
