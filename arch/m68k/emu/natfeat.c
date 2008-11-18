/*
 * natfeat.c - ARAnyM hardware support via Native Features (natfeats)
 *
 * Copyright (c) 2005 Petr Stehlik of ARAnyM dev team
 *
 * Reworked for Linux by Roman Zippel <zippel@linux-m68k.org>
 *
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 */

#include <linux/types.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/natfeat.h>

asm("\n"
"	.global nf_get_id,nf_call\n"
"nf_get_id:\n"
"	.short	0x7300\n"
"	rts\n"
"nf_call:\n"
"	.short	0x7301\n"
"	rts\n"
"1:	moveq.l	#0,%d0\n"
"	rts\n"
"	.section __ex_table,\"a\"\n"
"	.long	nf_get_id,1b\n"
"	.long	nf_call,1b\n"
"	.previous");
EXPORT_SYMBOL_GPL(nf_get_id);
EXPORT_SYMBOL_GPL(nf_call);

static int stderr_id;

static void nf_write(struct console *co, const char *str, unsigned int count)
{
	char buf[68];

	buf[64] = 0;
	while (count > 64) {
		memcpy(buf, str, 64);
		nf_call(stderr_id, buf);
		str += 64;
		count -= 64;
	}
	memcpy(buf, str, count);
	buf[count] = 0;
	nf_call(stderr_id, buf);
}

void nfprint(const char *fmt, ...)
{
	static char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, 256, fmt, ap);
	nf_call(nf_get_id("NF_STDERR"), buf);
	va_end(ap);
}

static struct console nf_console_driver = {
	.name	= "debug",
	.write	= nf_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

static int __init nf_debug_setup(char *arg)
{
	if (strcmp(arg, "emu"))
		return 0;

	stderr_id = nf_get_id("NF_STDERR");
	if (stderr_id)
		register_console(&nf_console_driver);
	return 0;
}

early_param("debug", nf_debug_setup);

static void nf_poweroff(void)
{
	long id = nf_get_id("NF_SHUTDOWN");

	if (id)
		nf_call(id);
}

void nf_init(void)
{
	unsigned long id, version;
	char buf[256];

	id = nf_get_id("NF_VERSION");
	if (!id)
		return;
	version = nf_call(id);

	id = nf_get_id("NF_NAME");
	if (!id)
		return;
	nf_call(id, buf, 256);
	buf[255] = 0;

	pr_info("NatFeats found (%s, %lu.%lu)\n", buf, version >> 16,
		version & 0xffff);

	mach_power_off = nf_poweroff;
}
