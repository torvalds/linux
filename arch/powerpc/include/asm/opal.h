/*
 * PowerNV OPAL definitions.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __OPAL_H
#define __OPAL_H

/****** Takeover interface ********/

/* PAPR H-Call used to querty the HAL existence and/or instanciate
 * it from within pHyp (tech preview only).
 *
 * This is exclusively used in prom_init.c
 */

#ifndef __ASSEMBLY__

struct opal_takeover_args {
	u64	k_image;		/* r4 */
	u64	k_size;			/* r5 */
	u64	k_entry;		/* r6 */
	u64	k_entry2;		/* r7 */
	u64	hal_addr;		/* r8 */
	u64	rd_image;		/* r9 */
	u64	rd_size;		/* r10 */
	u64	rd_loc;			/* r11 */
};

extern long opal_query_takeover(u64 *hal_size, u64 *hal_align);

extern long opal_do_takeover(struct opal_takeover_args *args);

extern int opal_enter_rtas(struct rtas_args *args,
			   unsigned long data,
			   unsigned long entry);


#endif /* __ASSEMBLY__ */

/****** OPAL APIs ******/


#endif /* __OPAL_H */
