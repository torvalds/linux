/*
 * IP32 basic setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 * Copyright (C) 2002, 2003, 2005 Ilya A. Volynets
 * Copyright (C) 2006 Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/param.h>
#include <linux/sched.h>

#include <asm/bootinfo.h>
#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/sgialib.h>
#include <asm/time.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/ip32/crime.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

extern void ip32_be_init(void);
extern void crime_init(void);

#ifdef CONFIG_SGI_O2MACE_ETH
/*
 * This is taken care of in here 'cause they say using Arc later on is
 * problematic
 */
extern char o2meth_eaddr[8];
static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if(*str == ':')
			str++;
		num = str2hexnum(*str++) << 4;
		num |= (str2hexnum(*str++));
		ea[i] = num;
	}
}
#endif

/* An arbitrary time; this can be decreased if reliability looks good */
#define WAIT_MS 10

void __init plat_time_init(void)
{
	printk(KERN_INFO "Calibrating system timer... ");
	write_c0_count(0);
	crime->timer = 0;
	while (crime->timer < CRIME_MASTER_FREQ * WAIT_MS / 1000) ;
	mips_hpt_frequency = read_c0_count() * 1000 / WAIT_MS;
	printk("%d MHz CPU detected\n", mips_hpt_frequency * 2 / 1000000);
}

void __init plat_mem_setup(void)
{
	board_be_init = ip32_be_init;

#ifdef CONFIG_SGI_O2MACE_ETH
	{
		char *mac = ArcGetEnvironmentVariable("eaddr");
		str2eaddr(o2meth_eaddr, mac);
	}
#endif

#if defined(CONFIG_SERIAL_CORE_CONSOLE)
	{
		char* con = ArcGetEnvironmentVariable("console");
		if (con && *con == 'd') {
			static char options[8] __initdata;
			char *baud = ArcGetEnvironmentVariable("dbaud");
			if (baud)
				strcpy(options, baud);
			add_preferred_console("ttyS", *(con + 1) == '2' ? 1 : 0,
					      baud ? options : NULL);
		}
	}
#endif
}
