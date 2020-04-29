/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.34 by: Mark Salter (msalter@redhat.com)
 */
#ifndef _ASM_C6X_MODULE_H
#define _ASM_C6X_MODULE_H

#include <asm-generic/module.h>

struct loaded_sections {
	unsigned int new_vaddr;
	unsigned int loaded;
};

#endif /* _ASM_C6X_MODULE_H */
