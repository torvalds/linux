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

#include <linux/init.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/natfeat.h>

extern long nf_get_id_phys(unsigned long feature_name);

asm("\n"
"	.global nf_get_id_phys,nf_call\n"
"nf_get_id_phys:\n"
"	.short	0x7300\n"
"	rts\n"
"nf_call:\n"
"	.short	0x7301\n"
"	rts\n"
"1:	moveq.l	#0,%d0\n"
"	rts\n"
"	.section __ex_table,\"a\"\n"
"	.long	nf_get_id_phys,1b\n"
"	.long	nf_call,1b\n"
"	.previous");
EXPORT_SYMBOL_GPL(nf_call);

long nf_get_id(const char *feature_name)
{
	/* feature_name may be in vmalloc()ed memory, so make a copy */
	char name_copy[32];
	size_t n;

	n = strlcpy(name_copy, feature_name, sizeof(name_copy));
	if (n >= sizeof(name_copy))
		return 0;

	return nf_get_id_phys(virt_to_phys(name_copy));
}
EXPORT_SYMBOL_GPL(nf_get_id);

void nfprint(const char *fmt, ...)
{
	static char buf[256];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, 256, fmt, ap);
	nf_call(nf_get_id("NF_STDERR"), virt_to_phys(buf));
	va_end(ap);
}

static void nf_poweroff(void)
{
	long id = nf_get_id("NF_SHUTDOWN");

	if (id)
		nf_call(id);
}

void __init nf_init(void)
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
	nf_call(id, virt_to_phys(buf), 256);
	buf[255] = 0;

	pr_info("NatFeats found (%s, %lu.%lu)\n", buf, version >> 16,
		version & 0xffff);

	register_platform_power_off(nf_poweroff);
}
