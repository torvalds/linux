/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __MEM_H__
#define __MEM_H__

extern int phys_mapping(unsigned long phys, unsigned long long *offset_out);

extern unsigned long uml_physmem;
static inline unsigned long to_phys(void *virt)
{
	return(((unsigned long) virt) - uml_physmem);
}

static inline void *to_virt(unsigned long phys)
{
	return((void *) uml_physmem + phys);
}

#endif
