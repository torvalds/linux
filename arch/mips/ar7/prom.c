/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Putting things on the screen/serial line using YAMONs facilities.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serial_reg.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/io.h>
#include <asm/bootinfo.h>

#include <asm/mach-ar7/ar7.h>
#include <asm/mach-ar7/prom.h>

#define MAX_ENTRY 80

struct env_var {
	char	*name;
	char	*value;
};

static struct env_var adam2_env[MAX_ENTRY];

char *prom_getenv(const char *name)
{
	int i;

	for (i = 0; (i < MAX_ENTRY) && adam2_env[i].name; i++)
		if (!strcmp(name, adam2_env[i].name))
			return adam2_env[i].value;

	return NULL;
}
EXPORT_SYMBOL(prom_getenv);

static void  __init ar7_init_cmdline(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		strlcat(arcs_cmdline, argv[i], COMMAND_LINE_SIZE);
		if (i < (argc - 1))
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
}

struct psbl_rec {
	u32	psbl_size;
	u32	env_base;
	u32	env_size;
	u32	ffs_base;
	u32	ffs_size;
};

static const char psp_env_version[] __initconst = "TIENV0.8";

struct psp_env_chunk {
	u8	num;
	u8	ctrl;
	u16	csum;
	u8	len;
	char	data[11];
} __packed;

struct psp_var_map_entry {
	u8	num;
	char	*value;
};

static const struct psp_var_map_entry psp_var_map[] = {
	{  1,	"cpufrequency" },
	{  2,	"memsize" },
	{  3,	"flashsize" },
	{  4,	"modetty0" },
	{  5,	"modetty1" },
	{  8,	"maca" },
	{  9,	"macb" },
	{ 28,	"sysfrequency" },
	{ 38,	"mipsfrequency" },
};

/*

Well-known variable (num is looked up in table above for matching variable name)
Example: cpufrequency=211968000
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---
| 01 |CTRL|CHECKSUM | 01 | _2 | _1 | _1 | _9 | _6 | _8 | _0 | _0 | _0 | \0 | FF
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---

Name=Value pair in a single chunk
Example: NAME=VALUE
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---
| 00 |CTRL|CHECKSUM | 01 | _N | _A | _M | _E | _0 | _V | _A | _L | _U | _E | \0
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---

Name=Value pair in 2 chunks (len is the number of chunks)
Example: bootloaderVersion=1.3.7.15
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---
| 00 |CTRL|CHECKSUM | 02 | _b | _o | _o | _t | _l | _o | _a | _d | _e | _r | _V
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---
| _e | _r | _s | _i | _o | _n | \0 | _1 | _. | _3 | _. | _7 | _. | _1 | _5 | \0
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+---

Data is padded with 0xFF

*/

#define PSP_ENV_SIZE  4096

static char psp_env_data[PSP_ENV_SIZE] = { 0, };

static char * __init lookup_psp_var_map(u8 num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(psp_var_map); i++)
		if (psp_var_map[i].num == num)
			return psp_var_map[i].value;

	return NULL;
}

static void __init add_adam2_var(char *name, char *value)
{
	int i;

	for (i = 0; i < MAX_ENTRY; i++) {
		if (!adam2_env[i].name) {
			adam2_env[i].name = name;
			adam2_env[i].value = value;
			return;
		} else if (!strcmp(adam2_env[i].name, name)) {
			adam2_env[i].value = value;
			return;
		}
	}
}

static int __init parse_psp_env(void *psp_env_base)
{
	int i, n;
	char *name, *value;
	struct psp_env_chunk *chunks = (struct psp_env_chunk *)psp_env_data;

	memcpy_fromio(chunks, psp_env_base, PSP_ENV_SIZE);

	i = 1;
	n = PSP_ENV_SIZE / sizeof(struct psp_env_chunk);
	while (i < n) {
		if ((chunks[i].num == 0xff) || ((i + chunks[i].len) > n))
			break;
		value = chunks[i].data;
		if (chunks[i].num) {
			name = lookup_psp_var_map(chunks[i].num);
		} else {
			name = value;
			value += strlen(name) + 1;
		}
		if (name)
			add_adam2_var(name, value);
		i += chunks[i].len;
	}
	return 0;
}

static void __init ar7_init_env(struct env_var *env)
{
	int i;
	struct psbl_rec *psbl = (struct psbl_rec *)(KSEG1ADDR(0x14000300));
	void *psp_env = (void *)KSEG1ADDR(psbl->env_base);

	if (strcmp(psp_env, psp_env_version) == 0) {
		parse_psp_env(psp_env);
	} else {
		for (i = 0; i < MAX_ENTRY; i++, env++)
			if (env->name)
				add_adam2_var(env->name, env->value);
	}
}

static void __init console_config(void)
{
#ifdef CONFIG_SERIAL_8250_CONSOLE
	char console_string[40];
	int baud = 0;
	char parity = '\0', bits = '\0', flow = '\0';
	char *s, *p;

	if (strstr(arcs_cmdline, "console="))
		return;

	s = prom_getenv("modetty0");
	if (s) {
		baud = simple_strtoul(s, &p, 10);
		s = p;
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

	if (flow == 'r')
		sprintf(console_string, " console=ttyS0,%d%c%c%c", baud,
			parity, bits, flow);
	else
		sprintf(console_string, " console=ttyS0,%d%c%c", baud, parity,
			bits);
	strlcat(arcs_cmdline, console_string, COMMAND_LINE_SIZE);
#endif
}

void __init prom_init(void)
{
	ar7_init_cmdline(fw_arg0, (char **)fw_arg1);
	ar7_init_env((struct env_var *)fw_arg2);
	console_config();

	ar7_gpio_init();
}

#define PORT(offset) (KSEG1ADDR(AR7_REGS_UART0 + (offset * 4)))
static inline unsigned int serial_in(int offset)
{
	return readl((void *)PORT(offset));
}

static inline void serial_out(int offset, int value)
{
	writel(value, (void *)PORT(offset));
}

int prom_putchar(char c)
{
	while ((serial_in(UART_LSR) & UART_LSR_TEMT) == 0)
		;
	serial_out(UART_TX, c);
	return 1;
}
