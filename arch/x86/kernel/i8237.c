/*
 * 8237A DMA controller suspend functions.
 *
 * Written by Pierre Ossman, 2005.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/syscore_ops.h>

#include <asm/dma.h>
#include <asm/x86_init.h>

/*
 * This module just handles suspend/resume issues with the
 * 8237A DMA controller (used for ISA and LPC).
 * Allocation is handled in kernel/dma.c and normal usage is
 * in asm/dma.h.
 */

static void i8237A_resume(void)
{
	unsigned long flags;
	int i;

	flags = claim_dma_lock();

	dma_outb(0, DMA1_RESET_REG);
	dma_outb(0, DMA2_RESET_REG);

	for (i = 0; i < 8; i++) {
		set_dma_addr(i, 0x000000);
		/* DMA count is a bit weird so this is not 0 */
		set_dma_count(i, 1);
	}

	/* Enable cascade DMA or channel 0-3 won't work */
	enable_dma(4);

	release_dma_lock(flags);
}

static struct syscore_ops i8237_syscore_ops = {
	.resume		= i8237A_resume,
};

static int __init i8237A_init_ops(void)
{
	/*
	 * From SKL PCH onwards, the legacy DMA device is removed in which the
	 * I/O ports (81h-83h, 87h, 89h-8Bh, 8Fh) related to it are removed
	 * as well. All removed ports must return 0xff for a inb() request.
	 *
	 * Note: DMA_PAGE_2 (port 0x81) should not be checked for detecting
	 * the presence of DMA device since it may be used by BIOS to decode
	 * LPC traffic for POST codes. Original LPC only decodes one byte of
	 * port 0x80 but some BIOS may choose to enhance PCH LPC port 0x8x
	 * decoding.
	 */
	if (dma_inb(DMA_PAGE_0) == 0xFF)
		return -ENODEV;

	/*
	 * It is not required to load this driver as newer SoC may not
	 * support 8237 DMA or bus mastering from LPC. Platform firmware
	 * must announce the support for such legacy devices via
	 * ACPI_FADT_LEGACY_DEVICES field in FADT table.
	 */
	if (x86_pnpbios_disabled() && dmi_get_bios_year() >= 2017)
		return -ENODEV;

	register_syscore_ops(&i8237_syscore_ops);
	return 0;
}
device_initcall(i8237A_init_ops);
