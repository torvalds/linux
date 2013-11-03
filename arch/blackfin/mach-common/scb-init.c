/*
 * arch/blackfin/mach-common/scb-init.c - reprogram system cross bar priority
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <asm/scb.h>

__attribute__((l1_text))
inline void scb_mi_write(unsigned long scb_mi_arbw, unsigned int slots,
		unsigned char *scb_mi_prio)
{
	unsigned int i;

	for (i = 0; i < slots; ++i)
		bfin_write32(scb_mi_arbw, (i << SCB_SLOT_OFFSET) | scb_mi_prio[i]);
}

__attribute__((l1_text))
inline void scb_mi_read(unsigned long scb_mi_arbw, unsigned int slots,
		unsigned char *scb_mi_prio)
{
	unsigned int i;

	for (i = 0; i < slots; ++i) {
		bfin_write32(scb_mi_arbw, (0xFF << SCB_SLOT_OFFSET) | i);
		scb_mi_prio[i] = bfin_read32(scb_mi_arbw);
	}
}

__attribute__((l1_text))
void init_scb(void)
{
	unsigned int i, j;
	unsigned char scb_tmp_prio[32];

	pr_info("Init System Crossbar\n");
	for (i = 0; scb_data[i].scb_mi_arbr > 0; ++i) {

		scb_mi_write(scb_data[i].scb_mi_arbw, scb_data[i].scb_mi_slots, scb_data[i].scb_mi_prio);

		pr_debug("scb priority at 0x%lx:\n", scb_data[i].scb_mi_arbr);
		scb_mi_read(scb_data[i].scb_mi_arbw, scb_data[i].scb_mi_slots, scb_tmp_prio);
		for (j = 0; j < scb_data[i].scb_mi_slots; ++j)
			pr_debug("slot %d = %d\n", j, scb_tmp_prio[j]);
	}

}
