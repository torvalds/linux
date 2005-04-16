/*
 * Setup the right wbflush routine for the different DECstations.
 *
 * Created with information from:
 *      DECstation 3100 Desktop Workstation Functional Specification
 *      DECstation 5000/200 KN02 System Module Functional Specification
 *      mipsel-linux-objdump --disassemble vmunix | grep "wbflush" :-)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Harald Koerfgen
 * Copyright (C) 2002 Maciej W. Rozycki
 */

#include <linux/init.h>

#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/wbflush.h>

static void wbflush_kn01(void);
static void wbflush_kn210(void);
static void wbflush_mips(void);

void (*__wbflush) (void);

void __init wbflush_setup(void)
{
	switch (mips_machtype) {
	case MACH_DS23100:
	case MACH_DS5000_200:	/* DS5000 3max */
		__wbflush = wbflush_kn01;
		break;
	case MACH_DS5100:	/* DS5100 MIPSMATE */
		__wbflush = wbflush_kn210;
		break;
	case MACH_DS5000_1XX:	/* DS5000/100 3min */
	case MACH_DS5000_XX:	/* Personal DS5000/2x */
	case MACH_DS5000_2X0:	/* DS5000/240 3max+ */
	case MACH_DS5900:	/* DS5900 bigmax */
	default:
		__wbflush = wbflush_mips;
		break;
	}
}

/*
 * For the DS3100 and DS5000/200 the R2020/R3220 writeback buffer functions
 * as part of Coprocessor 0.
 */
static void wbflush_kn01(void)
{
    asm(".set\tpush\n\t"
	".set\tnoreorder\n\t"
	"1:\tbc0f\t1b\n\t"
	"nop\n\t"
	".set\tpop");
}

/*
 * For the DS5100 the writeback buffer seems to be a part of Coprocessor 3.
 * But CP3 has to enabled first.
 */
static void wbflush_kn210(void)
{
    asm(".set\tpush\n\t"
	".set\tnoreorder\n\t"
	"mfc0\t$2,$12\n\t"
	"lui\t$3,0x8000\n\t"
	"or\t$3,$2,$3\n\t"
	"mtc0\t$3,$12\n\t"
	"nop\n"
	"1:\tbc3f\t1b\n\t"
	"nop\n\t"
	"mtc0\t$2,$12\n\t"
	"nop\n\t"
	".set\tpop"
	: : : "$2", "$3");
}

/*
 * I/O ASIC systems use a standard writeback buffer that gets flushed
 * upon an uncached read.
 */
static void wbflush_mips(void)
{
	__fast_iob();
}

#include <linux/module.h>

EXPORT_SYMBOL(__wbflush);
