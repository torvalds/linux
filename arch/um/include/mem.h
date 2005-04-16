/* 
 * Copyright (C) 2002, 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#ifndef __MEM_H__
#define __MEM_H__

#include "linux/types.h"

extern int phys_mapping(unsigned long phys, __u64 *offset_out);
extern int physmem_subst_mapping(void *virt, int fd, __u64 offset, int w);
extern int is_remapped(void *virt);
extern int physmem_remove_mapping(void *virt);
extern void physmem_forget_descriptor(int fd);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
