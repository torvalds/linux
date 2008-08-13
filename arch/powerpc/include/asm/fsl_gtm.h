/*
 * Freescale General-purpose Timers Module
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_FSL_GTM_H
#define __ASM_FSL_GTM_H

#include <linux/types.h>

struct gtm;

struct gtm_timer {
	unsigned int irq;

	struct gtm *gtm;
	bool requested;
	u8 __iomem *gtcfr;
	__be16 __iomem *gtmdr;
	__be16 __iomem *gtpsr;
	__be16 __iomem *gtcnr;
	__be16 __iomem *gtrfr;
	__be16 __iomem *gtevr;
};

extern struct gtm_timer *gtm_get_timer16(void);
extern struct gtm_timer *gtm_get_specific_timer16(struct gtm *gtm,
						  unsigned int timer);
extern void gtm_put_timer16(struct gtm_timer *tmr);
extern int gtm_set_timer16(struct gtm_timer *tmr, unsigned long usec,
			     bool reload);
extern int gtm_set_exact_timer16(struct gtm_timer *tmr, u16 usec,
				 bool reload);
extern void gtm_stop_timer16(struct gtm_timer *tmr);
extern void gtm_ack_timer16(struct gtm_timer *tmr, u16 events);

#endif /* __ASM_FSL_GTM_H */
