/*
 * Copyright 2012 Pavel Machek <pavel@denx.de>
 * Copyright (C) 2012 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MACH_CORE_H
#define __MACH_CORE_H

#define SOCFPGA_RSTMGR_CTRL	0x04
#define SOCFPGA_RSTMGR_MODPERRST	0x14
#define SOCFPGA_RSTMGR_BRGMODRST	0x1c

/* System Manager bits */
#define RSTMGR_CTRL_SWCOLDRSTREQ	0x1	/* Cold Reset */
#define RSTMGR_CTRL_SWWARMRSTREQ	0x2	/* Warm Reset */

extern void socfpga_secondary_startup(void);
extern void __iomem *socfpga_scu_base_addr;

extern void socfpga_init_clocks(void);
extern void socfpga_sysmgr_init(void);

extern void __iomem *sys_manager_base_addr;
extern void __iomem *rst_manager_base_addr;

extern struct smp_operations socfpga_smp_ops;
extern char secondary_trampoline, secondary_trampoline_end;

extern unsigned long cpu1start_addr;

#define SOCFPGA_SCU_VIRT_BASE   0xfffec000

#endif
