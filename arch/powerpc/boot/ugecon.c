/*
 * arch/powerpc/boot/ugecon.c
 *
 * USB Gecko bootwrapper console.
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#include <stddef.h>
#include "stdio.h"
#include "types.h"
#include "io.h"
#include "ops.h"


#define EXI_CLK_32MHZ           5

#define EXI_CSR                 0x00
#define   EXI_CSR_CLKMASK       (0x7<<4)
#define     EXI_CSR_CLK_32MHZ   (EXI_CLK_32MHZ<<4)
#define   EXI_CSR_CSMASK        (0x7<<7)
#define     EXI_CSR_CS_0        (0x1<<7)  /* Chip Select 001 */

#define EXI_CR                  0x0c
#define   EXI_CR_TSTART         (1<<0)
#define   EXI_CR_WRITE		(1<<2)
#define   EXI_CR_READ_WRITE     (2<<2)
#define   EXI_CR_TLEN(len)      (((len)-1)<<4)

#define EXI_DATA                0x10


/* virtual address base for input/output, retrieved from device tree */
static void *ug_io_base;


static u32 ug_io_transaction(u32 in)
{
	u32 *csr_reg = ug_io_base + EXI_CSR;
	u32 *data_reg = ug_io_base + EXI_DATA;
	u32 *cr_reg = ug_io_base + EXI_CR;
	u32 csr, data, cr;

	/* select */
	csr = EXI_CSR_CLK_32MHZ | EXI_CSR_CS_0;
	out_be32(csr_reg, csr);

	/* read/write */
	data = in;
	out_be32(data_reg, data);
	cr = EXI_CR_TLEN(2) | EXI_CR_READ_WRITE | EXI_CR_TSTART;
	out_be32(cr_reg, cr);

	while (in_be32(cr_reg) & EXI_CR_TSTART)
		barrier();

	/* deselect */
	out_be32(csr_reg, 0);

	data = in_be32(data_reg);
	return data;
}

static int ug_is_txfifo_ready(void)
{
	return ug_io_transaction(0xc0000000) & 0x04000000;
}

static void ug_raw_putc(char ch)
{
	ug_io_transaction(0xb0000000 | (ch << 20));
}

static void ug_putc(char ch)
{
	int count = 16;

	if (!ug_io_base)
		return;

	while (!ug_is_txfifo_ready() && count--)
		barrier();
	if (count >= 0)
		ug_raw_putc(ch);
}

void ug_console_write(const char *buf, int len)
{
	char *b = (char *)buf;

	while (len--) {
		if (*b == '\n')
			ug_putc('\r');
		ug_putc(*b++);
	}
}

static int ug_is_adapter_present(void)
{
	if (!ug_io_base)
		return 0;
	return ug_io_transaction(0x90000000) == 0x04700000;
}

static void *ug_grab_exi_io_base(void)
{
	u32 v;
	void *devp;

	devp = find_node_by_compatible(NULL, "nintendo,flipper-exi");
	if (devp == NULL)
		goto err_out;
	if (getprop(devp, "virtual-reg", &v, sizeof(v)) != sizeof(v))
		goto err_out;

	return (void *)v;

err_out:
	return NULL;
}

void *ug_probe(void)
{
	void *exi_io_base;
	int i;

	exi_io_base = ug_grab_exi_io_base();
	if (!exi_io_base)
		return NULL;

	/* look for a usbgecko on memcard slots A and B */
	for (i = 0; i < 2; i++) {
		ug_io_base = exi_io_base + 0x14 * i;
		if (ug_is_adapter_present())
			break;
	}
	if (i == 2)
		ug_io_base = NULL;
	return ug_io_base;
}

