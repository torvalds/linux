/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_TCM_H__
#define __ASM_TCM_H__

#include <linux/ioport.h>
#include <linux/list.h>

struct tcm_allocation {
	struct list_head list;
	unsigned int tag;
	unsigned long addr;
	unsigned long size;
};

/*
 * TCM memory region descriptor.
 */
struct tcm_region {
	unsigned int tag;
	struct resource res;
};

#define TCM_INVALID_TAG	0xffffffff

unsigned long tcm_alloc(unsigned int tag, size_t len);
void tcm_free(unsigned int tag, unsigned long addr, size_t len);
unsigned int tcm_lookup_tag(unsigned long p);

int tcm_add_region(struct tcm_region *reg);

#endif
