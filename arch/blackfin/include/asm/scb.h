/*
 * arch/blackfin/mach-common/scb-init.c - reprogram system cross bar priority
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#define SCB_SLOT_OFFSET	24
#define SCB_MI_MAX_SLOT 32

struct scb_mi_prio {
	unsigned long scb_mi_arbr;
	unsigned long scb_mi_arbw;
	unsigned char scb_mi_slots;
	unsigned char scb_mi_prio[SCB_MI_MAX_SLOT];
};

extern struct scb_mi_prio scb_data[];

extern void init_scb(void);
