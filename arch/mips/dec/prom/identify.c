// SPDX-License-Identifier: GPL-2.0
/*
 * identify.c: machine identification code.
 *
 * Copyright (C) 1998 Harald Koerfgen and Paul M. Antoine
 * Copyright (C) 2002, 2003, 2004, 2005  Maciej W. Rozycki
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/bootinfo.h>

#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/kn01.h>
#include <asm/dec/kn02.h>
#include <asm/dec/kn02ba.h>
#include <asm/dec/kn02ca.h>
#include <asm/dec/kn03.h>
#include <asm/dec/kn230.h>
#include <asm/dec/prom.h>
#include <asm/dec/system.h>

#include "dectypes.h"

static const char *dec_system_strings[] = {
	[MACH_DSUNKNOWN]	"unknown DECstation",
	[MACH_DS23100]		"DECstation 2100/3100",
	[MACH_DS5100]		"DECsystem 5100",
	[MACH_DS5000_200]	"DECstation 5000/200",
	[MACH_DS5000_1XX]	"DECstation 5000/1xx",
	[MACH_DS5000_XX]	"Personal DECstation 5000/xx",
	[MACH_DS5000_2X0]	"DECstation 5000/2x0",
	[MACH_DS5400]		"DECsystem 5400",
	[MACH_DS5500]		"DECsystem 5500",
	[MACH_DS5800]		"DECsystem 5800",
	[MACH_DS5900]		"DECsystem 5900",
};

const char *get_system_type(void)
{
#define STR_BUF_LEN	64
	static char system[STR_BUF_LEN];
	static int called = 0;

	if (called == 0) {
		called = 1;
		snprintf(system, STR_BUF_LEN, "Digital %s",
			 dec_system_strings[mips_machtype]);
	}

	return system;
}


/*
 * Setup essential system-specific memory addresses.  We need them
 * early.  Semantically the functions belong to prom/init.c, but they
 * are compact enough we want them inlined. --macro
 */
volatile u8 *dec_rtc_base;

EXPORT_SYMBOL(dec_rtc_base);

static inline void prom_init_kn01(void)
{
	dec_kn_slot_base = KN01_SLOT_BASE;
	dec_kn_slot_size = KN01_SLOT_SIZE;

	dec_rtc_base = (void *)CKSEG1ADDR(dec_kn_slot_base + KN01_RTC);
}

static inline void prom_init_kn230(void)
{
	dec_kn_slot_base = KN01_SLOT_BASE;
	dec_kn_slot_size = KN01_SLOT_SIZE;

	dec_rtc_base = (void *)CKSEG1ADDR(dec_kn_slot_base + KN01_RTC);
}

static inline void prom_init_kn02(void)
{
	dec_kn_slot_base = KN02_SLOT_BASE;
	dec_kn_slot_size = KN02_SLOT_SIZE;
	dec_tc_bus = 1;

	dec_rtc_base = (void *)CKSEG1ADDR(dec_kn_slot_base + KN02_RTC);
}

static inline void prom_init_kn02xa(void)
{
	dec_kn_slot_base = KN02XA_SLOT_BASE;
	dec_kn_slot_size = IOASIC_SLOT_SIZE;
	dec_tc_bus = 1;

	ioasic_base = (void *)CKSEG1ADDR(dec_kn_slot_base + IOASIC_IOCTL);
	dec_rtc_base = (void *)CKSEG1ADDR(dec_kn_slot_base + IOASIC_TOY);
}

static inline void prom_init_kn03(void)
{
	dec_kn_slot_base = KN03_SLOT_BASE;
	dec_kn_slot_size = IOASIC_SLOT_SIZE;
	dec_tc_bus = 1;

	ioasic_base = (void *)CKSEG1ADDR(dec_kn_slot_base + IOASIC_IOCTL);
	dec_rtc_base = (void *)CKSEG1ADDR(dec_kn_slot_base + IOASIC_TOY);
}


void __init prom_identify_arch(u32 magic)
{
	unsigned char dec_cpunum, dec_firmrev, dec_etc, dec_systype;
	u32 dec_sysid;

	if (!prom_is_rex(magic)) {
		dec_sysid = simple_strtoul(prom_getenv("systype"),
					   (char **)0, 0);
	} else {
		dec_sysid = rex_getsysid();
		if (dec_sysid == 0) {
			printk("Zero sysid returned from PROM! "
			       "Assuming a PMAX-like machine.\n");
			dec_sysid = 1;
		}
	}

	dec_cpunum = (dec_sysid & 0xff000000) >> 24;
	dec_systype = (dec_sysid & 0xff0000) >> 16;
	dec_firmrev = (dec_sysid & 0xff00) >> 8;
	dec_etc = dec_sysid & 0xff;

	/*
	 * FIXME: This may not be an exhaustive list of DECStations/Servers!
	 * Put all model-specific initialisation calls here.
	 */
	switch (dec_systype) {
	case DS2100_3100:
		mips_machtype = MACH_DS23100;
		prom_init_kn01();
		break;
	case DS5100:		/* DS5100 MIPSMATE */
		mips_machtype = MACH_DS5100;
		prom_init_kn230();
		break;
	case DS5000_200:	/* DS5000 3max */
		mips_machtype = MACH_DS5000_200;
		prom_init_kn02();
		break;
	case DS5000_1XX:	/* DS5000/100 3min */
		mips_machtype = MACH_DS5000_1XX;
		prom_init_kn02xa();
		break;
	case DS5000_2X0:	/* DS5000/240 3max+ or DS5900 bigmax */
		mips_machtype = MACH_DS5000_2X0;
		prom_init_kn03();
		if (!(ioasic_read(IO_REG_SIR) & KN03_IO_INR_3MAXP))
			mips_machtype = MACH_DS5900;
		break;
	case DS5000_XX:		/* Personal DS5000/xx maxine */
		mips_machtype = MACH_DS5000_XX;
		prom_init_kn02xa();
		break;
	case DS5800:		/* DS5800 Isis */
		mips_machtype = MACH_DS5800;
		break;
	case DS5400:		/* DS5400 MIPSfair */
		mips_machtype = MACH_DS5400;
		break;
	case DS5500:		/* DS5500 MIPSfair-2 */
		mips_machtype = MACH_DS5500;
		break;
	default:
		mips_machtype = MACH_DSUNKNOWN;
		break;
	}

	if (mips_machtype == MACH_DSUNKNOWN)
		printk("This is an %s, id is %x\n",
		       dec_system_strings[mips_machtype], dec_systype);
	else
		printk("This is a %s\n", dec_system_strings[mips_machtype]);
}
