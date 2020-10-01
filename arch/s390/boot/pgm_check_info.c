// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/lowcore.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include "boot.h"

const char hex_asc[] = "0123456789abcdef";

#define add_val_as_hex(dst, val)					       \
	__add_val_as_hex(dst, (const unsigned char *)&val, sizeof(val))

static char *__add_val_as_hex(char *dst, const unsigned char *src, size_t count)
{
	while (count--)
		dst = hex_byte_pack(dst, *src++);
	return dst;
}

static char *add_str(char *dst, char *src)
{
	strcpy(dst, src);
	return dst + strlen(dst);
}

void print_pgm_check_info(void)
{
	struct psw_bits *psw = &psw_bits(S390_lowcore.psw_save_area);
	unsigned short ilc = S390_lowcore.pgm_ilc >> 1;
	char buf[256];
	int row, col;
	char *p;

	add_str(buf, "Linux version ");
	strlcat(buf, kernel_version, sizeof(buf) - 1);
	strlcat(buf, "\n", sizeof(buf));
	sclp_early_printk(buf);

	p = add_str(buf, "Kernel fault: interruption code ");
	p = add_val_as_hex(buf + strlen(buf), S390_lowcore.pgm_code);
	p = add_str(p, " ilc:");
	*p++ = hex_asc_lo(ilc);
	add_str(p, "\n");
	sclp_early_printk(buf);

	if (kaslr_enabled) {
		p = add_str(buf, "Kernel random base: ");
		p = add_val_as_hex(p, __kaslr_offset);
		add_str(p, "\n");
		sclp_early_printk(buf);
	}

	p = add_str(buf, "PSW : ");
	p = add_val_as_hex(p, S390_lowcore.psw_save_area.mask);
	p = add_str(p, " ");
	p = add_val_as_hex(p, S390_lowcore.psw_save_area.addr);
	add_str(p, "\n");
	sclp_early_printk(buf);

	p = add_str(buf, "      R:");
	*p++ = hex_asc_lo(psw->per);
	p = add_str(p, " T:");
	*p++ = hex_asc_lo(psw->dat);
	p = add_str(p, " IO:");
	*p++ = hex_asc_lo(psw->io);
	p = add_str(p, " EX:");
	*p++ = hex_asc_lo(psw->ext);
	p = add_str(p, " Key:");
	*p++ = hex_asc_lo(psw->key);
	p = add_str(p, " M:");
	*p++ = hex_asc_lo(psw->mcheck);
	p = add_str(p, " W:");
	*p++ = hex_asc_lo(psw->wait);
	p = add_str(p, " P:");
	*p++ = hex_asc_lo(psw->pstate);
	p = add_str(p, " AS:");
	*p++ = hex_asc_lo(psw->as);
	p = add_str(p, " CC:");
	*p++ = hex_asc_lo(psw->cc);
	p = add_str(p, " PM:");
	*p++ = hex_asc_lo(psw->pm);
	p = add_str(p, " RI:");
	*p++ = hex_asc_lo(psw->ri);
	p = add_str(p, " EA:");
	*p++ = hex_asc_lo(psw->eaba);
	add_str(p, "\n");
	sclp_early_printk(buf);

	for (row = 0; row < 4; row++) {
		p = add_str(buf, row == 0 ? "GPRS:" : "     ");
		for (col = 0; col < 4; col++) {
			p = add_str(p, " ");
			p = add_val_as_hex(p, S390_lowcore.gpregs_save_area[row * 4 + col]);
		}
		add_str(p, "\n");
		sclp_early_printk(buf);
	}
}
