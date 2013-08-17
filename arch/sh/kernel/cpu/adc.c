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

	csr = __raw_readb(ADCSR);
	csr = channel | ADCSR_ADST | ADCSR_CKS;
	__raw_writeb(csr, ADCSR);

	do {
		csr = __raw_readb(ADCSR);
	} while ((csr & ADCSR_ADF) == 0);

	csr &= ~(ADCSR_ADF | ADCSR_ADST);
	__raw_writeb(csr, ADCSR);

	return (((__raw_readb(ADDRAH + off) << 8) |
		__raw_readb(ADDRAL + off)) >> 6);
}

EXPORT_SYMBOL(adc_single);
