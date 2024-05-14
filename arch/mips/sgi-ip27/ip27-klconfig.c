// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/param.h>
#include <linux/timex.h>
#include <linux/mm.h>

#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/sn/gda.h>

klinfo_t *find_component(lboard_t *brd, klinfo_t *kli, unsigned char struct_type)
{
	int index, j;

	if (kli == (klinfo_t *)NULL) {
		index = 0;
	} else {
		for (j = 0; j < KLCF_NUM_COMPS(brd); j++)
			if (kli == KLCF_COMP(brd, j))
				break;
		index = j;
		if (index == KLCF_NUM_COMPS(brd)) {
			printk("find_component: Bad pointer: 0x%p\n", kli);
			return (klinfo_t *)NULL;
		}
		index++;		/* next component */
	}

	for (; index < KLCF_NUM_COMPS(brd); index++) {
		kli = KLCF_COMP(brd, index);
		if (KLCF_COMP_TYPE(kli) == struct_type)
			return kli;
	}

	/* Didn't find it. */
	return (klinfo_t *)NULL;
}

klinfo_t *find_first_component(lboard_t *brd, unsigned char struct_type)
{
	return find_component(brd, (klinfo_t *)NULL, struct_type);
}

lboard_t *find_lboard(lboard_t *start, unsigned char brd_type)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (start->brd_type == brd_type)
			return start;
		start = KLCF_NEXT(start);
	}
	/* Didn't find it. */
	return (lboard_t *)NULL;
}

lboard_t *find_lboard_class(lboard_t *start, unsigned char brd_type)
{
	/* Search all boards stored on this node. */
	while (start) {
		if (KLCLASS(start->brd_type) == KLCLASS(brd_type))
			return start;
		start = KLCF_NEXT(start);
	}

	/* Didn't find it. */
	return (lboard_t *)NULL;
}
