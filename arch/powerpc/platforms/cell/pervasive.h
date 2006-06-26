/*
 * Cell Pervasive Monitor and Debug interface and HW structures
 *
 * (C) Copyright IBM Corporation 2005
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          David J. Erb (djerb@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef PERVASIVE_H
#define PERVASIVE_H

extern void cbe_pervasive_init(void);
extern void cbe_system_error_exception(struct pt_regs *regs);
extern void cbe_maintenance_exception(struct pt_regs *regs);
extern void cbe_thermal_exception(struct pt_regs *regs);

#endif
