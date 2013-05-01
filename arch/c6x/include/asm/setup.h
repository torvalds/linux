/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_SETUP_H
#define _ASM_C6X_SETUP_H

#include <uapi/asm/setup.h>

#ifndef __ASSEMBLY__
extern char c6x_command_line[COMMAND_LINE_SIZE];

extern int c6x_add_memory(phys_addr_t start, unsigned long size);

extern unsigned long ram_start;
extern unsigned long ram_end;

extern int c6x_num_cores;
extern unsigned int c6x_silicon_rev;
extern unsigned int c6x_devstat;
extern unsigned char c6x_fuse_mac[6];

extern void machine_init(unsigned long dt_ptr);
extern void time_init(void);

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_C6X_SETUP_H */
