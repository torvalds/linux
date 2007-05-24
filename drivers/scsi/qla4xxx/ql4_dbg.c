/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

void qla4xxx_dump_buffer(void *b, uint32_t size)
{
	uint32_t cnt;
	uint8_t *c = b;

	printk(" 0   1	 2   3	 4   5	 6   7	 8   9	Ah  Bh	Ch  Dh	Eh  "
	       "Fh\n");
	printk("------------------------------------------------------------"
	       "--\n");
	for (cnt = 0; cnt < size; cnt++, c++) {
		printk(KERN_DEBUG "%02x", *c);
		if (!(cnt % 16))
			printk(KERN_DEBUG "\n");

		else
			printk(KERN_DEBUG "  ");
	}
	if (cnt % 16)
		printk(KERN_DEBUG "\n");
}

