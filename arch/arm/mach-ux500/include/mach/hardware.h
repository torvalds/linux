/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * U8500 hardware definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

/* macros to get at IO space when running virtually
 * We dont map all the peripherals, let ioremap do
 * this for us. We map only very basic peripherals here.
 */
#define U8500_IO_VIRTUAL	0xf0000000
#define U8500_IO_PHYSICAL	0xa0000000

/* this macro is used in assembly, so no cast */
#define IO_ADDRESS(x)           \
	(((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + U8500_IO_VIRTUAL)

/* typesafe io address */
#define __io_address(n)		__io(IO_ADDRESS(n))
/* used by some plat-nomadik code */
#define io_p2v(n)		__io_address(n)

#include <mach/db8500-regs.h>
#include <mach/db5500-regs.h>

/* ST-Ericsson modified pl022 id */
#define SSP_PER_ID		0x01080022

#ifndef __ASSEMBLY__

#include <asm/cputype.h>

static inline bool cpu_is_u8500(void)
{
#ifdef CONFIG_UX500_SOC_DB8500
	return 1;
#else
	return 0;
#endif
}

#define CPUID_DB8500ED	0x410fc090
#define CPUID_DB8500V1	0x411fc091
#define CPUID_DB8500V2	0x412fc091

static inline bool cpu_is_u8500ed(void)
{
	return cpu_is_u8500() && (read_cpuid_id() == CPUID_DB8500ED);
}

static inline bool cpu_is_u8500v1(void)
{
	return cpu_is_u8500() && (read_cpuid_id() == CPUID_DB8500V1);
}

static inline bool cpu_is_u8500v2(void)
{
	return cpu_is_u8500() && (read_cpuid_id() == CPUID_DB8500V2);
}

#ifdef CONFIG_UX500_SOC_DB8500
bool cpu_is_u8500v10(void);
bool cpu_is_u8500v11(void);
bool cpu_is_u8500v20(void);
#else
static inline bool cpu_is_u8500v10(void) { return false; }
static inline bool cpu_is_u8500v11(void) { return false; }
static inline bool cpu_is_u8500v20(void) { return false; }
#endif

static inline bool cpu_is_u5500(void)
{
#ifdef CONFIG_UX500_SOC_DB5500
	return 1;
#else
	return 0;
#endif
}

#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)
#define ux500_unknown_soc()	BUG()

#endif

#endif				/* __MACH_HARDWARE_H */
