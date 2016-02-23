/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/io.h>

#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/mips-boards/generic.h>
#include <asm/fw/fw.h>

extern char except_vec_nmi;
extern char except_vec_ejtag_debug;

#ifdef CONFIG_SERIAL_8250_CONSOLE
static void __init console_config(void)
{
	char console_string[40];
	int baud = 0;
	char parity = '\0', bits = '\0', flow = '\0';
	char *s;

	if ((strstr(fw_getcmdline(), "console=")) == NULL) {
		s = fw_getenv("modetty0");
		if (s) {
			while (*s >= '0' && *s <= '9')
				baud = baud*10 + *s++ - '0';
			if (*s == ',')
				s++;
			if (*s)
				parity = *s++;
			if (*s == ',')
				s++;
			if (*s)
				bits = *s++;
			if (*s == ',')
				s++;
			if (*s == 'h')
				flow = 'r';
		}
		if (baud == 0)
			baud = 38400;
		if (parity != 'n' && parity != 'o' && parity != 'e')
			parity = 'n';
		if (bits != '7' && bits != '8')
			bits = '8';
		if (flow == '\0')
			flow = 'r';
		sprintf(console_string, " console=ttyS0,%d%c%c%c", baud,
			parity, bits, flow);
		strcat(fw_getcmdline(), console_string);
	}
}
#endif

static void __init mips_nmi_setup(void)
{
	void *base;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa80) :
		(void *)(CAC_BASE + 0x380);
#ifdef CONFIG_CPU_MICROMIPS
	/*
	 * Decrement the exception vector address by one for microMIPS.
	 */
	memcpy(base, (&except_vec_nmi - 1), 0x80);

	/*
	 * This is a hack. We do not know if the boot loader was built with
	 * microMIPS instructions or not. If it was not, the NMI exception
	 * code at 0x80000a80 will be taken in MIPS32 mode. The hand coded
	 * assembly below forces us into microMIPS mode if we are a pure
	 * microMIPS kernel. The assembly instructions are:
	 *
	 *  3C1A8000   lui       k0,0x8000
	 *  375A0381   ori       k0,k0,0x381
	 *  03400008   jr        k0
	 *  00000000   nop
	 *
	 * The mode switch occurs by jumping to the unaligned exception
	 * vector address at 0x80000381 which would have been 0x80000380
	 * in MIPS32 mode. The jump to the unaligned address transitions
	 * us into microMIPS mode.
	 */
	if (!cpu_has_veic) {
		void *base2 = (void *)(CAC_BASE + 0xa80);
		*((unsigned int *)base2) = 0x3c1a8000;
		*((unsigned int *)base2 + 1) = 0x375a0381;
		*((unsigned int *)base2 + 2) = 0x03400008;
		*((unsigned int *)base2 + 3) = 0x00000000;
		flush_icache_range((unsigned long)base2,
			(unsigned long)base2 + 0x10);
	}
#else
	memcpy(base, &except_vec_nmi, 0x80);
#endif
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

static void __init mips_ejtag_setup(void)
{
	void *base;

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa00) :
		(void *)(CAC_BASE + 0x300);
#ifdef CONFIG_CPU_MICROMIPS
	/* Deja vu... */
	memcpy(base, (&except_vec_ejtag_debug - 1), 0x80);
	if (!cpu_has_veic) {
		void *base2 = (void *)(CAC_BASE + 0xa00);
		*((unsigned int *)base2) = 0x3c1a8000;
		*((unsigned int *)base2 + 1) = 0x375a0301;
		*((unsigned int *)base2 + 2) = 0x03400008;
		*((unsigned int *)base2 + 3) = 0x00000000;
		flush_icache_range((unsigned long)base2,
			(unsigned long)base2 + 0x10);
	}
#else
	memcpy(base, &except_vec_ejtag_debug, 0x80);
#endif
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

void __init prom_init(void)
{
	board_nmi_handler_setup = mips_nmi_setup;
	board_ejtag_handler_setup = mips_ejtag_setup;

	fw_init_cmdline();
#ifdef CONFIG_EARLY_PRINTK
	if ((strstr(fw_getcmdline(), "console=ttyS0")) != NULL)
		fw_init_early_console(0);
	else if ((strstr(fw_getcmdline(), "console=ttyS1")) != NULL)
		fw_init_early_console(1);
#endif
#ifdef CONFIG_SERIAL_8250_CONSOLE
	if ((strstr(fw_getcmdline(), "console=")) == NULL)
		strcat(fw_getcmdline(), " console=ttyS0,38400n8r");
	console_config();
#endif
}

void __init prom_free_prom_memory(void)
{
}
