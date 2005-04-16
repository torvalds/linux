/*
 * linux/arch/sh/kernel/adc.c -- SH3 on-chip ADC support
 *
 *  Copyright (C) 2004  Andriy Skulysh <askulysh@image.kiev.ua>
 */

#include <linux/module.h>
#include <asm/adc.h>
#include <asm/io.h>


int adc_single(unsigned int channel)
{
	int off;
	unsigned char csr;

	if (channel >= 8) return -1;

	off = (channel & 0x03) << 2;

	csr = ctrl_inb(ADCSR);
	csr = channel | ADCSR_ADST | ADCSR_CKS;
	ctrl_outb(csr, ADCSR);

	do {
		csr = ctrl_inb(ADCSR);
	} while ((csr & ADCSR_ADF) == 0);

	csr &= ~(ADCSR_ADF | ADCSR_ADST);
	ctrl_outb(csr, ADCSR);

	return (((ctrl_inb(ADDRAH + off) << 8) |
		ctrl_inb(ADDRAL + off)) >> 6);
}

EXPORT_SYMBOL(adc_single);
