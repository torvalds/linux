/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2010 Texas Instruments Incorporated
 *  Author: Mark Salter (msalter@redhat.com)
 */
#ifndef _ASM_C6X_PROCINFO_H
#define _ASM_C6X_PROCINFO_H

#ifdef __KERNEL__

struct proc_info_list {
	unsigned int		cpu_val;
	unsigned int		cpu_mask;
	const char		*arch_name;
	const char		*elf_name;
	unsigned int		elf_hwcap;
};

#else	/* __KERNEL__ */
#include <asm/elf.h>
#warning "Please include asm/elf.h instead"
#endif	/* __KERNEL__ */

#endif	/* _ASM_C6X_PROCINFO_H */
