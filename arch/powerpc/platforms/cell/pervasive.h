/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cell Pervasive Monitor and Debug interface and HW structures
 *
 * (C) Copyright IBM Corporation 2005
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          David J. Erb (djerb@us.ibm.com)
 */


#ifndef PERVASIVE_H
#define PERVASIVE_H

extern void cbe_pervasive_init(void);
extern void cbe_system_error_exception(struct pt_regs *regs);
extern void cbe_maintenance_exception(struct pt_regs *regs);
extern void cbe_thermal_exception(struct pt_regs *regs);

#ifdef CONFIG_PPC_IBM_CELL_RESETBUTTON
extern int cbe_sysreset_hack(void);
#else
static inline int cbe_sysreset_hack(void)
{
	return 1;
}
#endif /* CONFIG_PPC_IBM_CELL_RESETBUTTON */

#endif
