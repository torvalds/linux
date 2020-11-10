// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/lowcore.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include <stdarg.h>
#include "boot.h"

const char hex_asc[] = "0123456789abcdef";

static char *as_hex(char *dst, unsigned long val, int pad)
{
	char *p, *end = p = dst + max(pad, (int)__fls(val | 1) / 4 + 1);

	for (*p-- = 0; p >= dst; val >>= 4)
		*p-- = hex_asc[val & 0x0f];
	return end;
}

void decompressor_printk(const char *fmt, ...)
{
	char buf[1024] = { 0 };
	char *end = buf + sizeof(buf) - 1; /* make sure buf is 0 terminated */
	unsigned long pad;
	char *p = buf;
	va_list args;

	va_start(args, fmt);
	for (; p < end && *fmt; fmt++) {
		if (*fmt != '%') {
			*p++ = *fmt;
			continue;
		}
		pad = isdigit(*++fmt) ? simple_strtol(fmt, (char **)&fmt, 10) : 0;
		switch (*fmt) {
		case 's':
			p = buf + strlcat(buf, va_arg(args, char *), sizeof(buf));
			break;
		case 'l':
			if (*++fmt != 'x' || end - p <= max(sizeof(long) * 2, pad))
				goto out;
			p = as_hex(p, va_arg(args, unsigned long), pad);
			break;
		case 'x':
			if (end - p <= max(sizeof(int) * 2, pad))
				goto out;
			p = as_hex(p, va_arg(args, unsigned int), pad);
			break;
		default:
			goto out;
		}
	}
out:
	va_end(args);
	sclp_early_printk(buf);
}

void print_pgm_check_info(void)
{
	unsigned long *gpregs = (unsigned long *)S390_lowcore.gpregs_save_area;
	struct psw_bits *psw = &psw_bits(S390_lowcore.psw_save_area);

	decompressor_printk("Linux version %s\n", kernel_version);
	decompressor_printk("Kernel fault: interruption code %04x ilc:%x\n",
			    S390_lowcore.pgm_code, S390_lowcore.pgm_ilc >> 1);
	if (kaslr_enabled)
		decompressor_printk("Kernel random base: %lx\n", __kaslr_offset);
	decompressor_printk("PSW : %016lx %016lx\n",
			    S390_lowcore.psw_save_area.mask,
			    S390_lowcore.psw_save_area.addr);
	decompressor_printk(
		"      R:%x T:%x IO:%x EX:%x Key:%x M:%x W:%x P:%x AS:%x CC:%x PM:%x RI:%x EA:%x\n",
		psw->per, psw->dat, psw->io, psw->ext, psw->key, psw->mcheck,
		psw->wait, psw->pstate, psw->as, psw->cc, psw->pm, psw->ri,
		psw->eaba);
	decompressor_printk("GPRS: %016lx %016lx %016lx %016lx\n",
			    gpregs[0], gpregs[1], gpregs[2], gpregs[3]);
	decompressor_printk("      %016lx %016lx %016lx %016lx\n",
			    gpregs[4], gpregs[5], gpregs[6], gpregs[7]);
	decompressor_printk("      %016lx %016lx %016lx %016lx\n",
			    gpregs[8], gpregs[9], gpregs[10], gpregs[11]);
	decompressor_printk("      %016lx %016lx %016lx %016lx\n",
			    gpregs[12], gpregs[13], gpregs[14], gpregs[15]);
}
