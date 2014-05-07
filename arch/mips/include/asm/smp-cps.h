/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_SMP_CPS_H__
#define __MIPS_ASM_SMP_CPS_H__

#ifndef __ASSEMBLY__

struct boot_config {
	unsigned int core;
	unsigned int vpe;
	unsigned long pc;
	unsigned long sp;
	unsigned long gp;
};

extern struct boot_config mips_cps_bootcfg;

extern void mips_cps_core_entry(void);

#else /* __ASSEMBLY__ */

.extern mips_cps_bootcfg;

#endif /* __ASSEMBLY__ */
#endif /* __MIPS_ASM_SMP_CPS_H__ */
